use anyhow::{anyhow, Result};
use std::env;
use std::ffi::OsStr;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;

const ENV_LLVM_PREFIX: &str = "NYXSTONE_LLVM_PREFIX";

fn main() {
    let headers = [
        "nyxstone/include/nyxstone.h",
        "nyxstone/src/ELFStreamerWrapper.h",
        "nyxstone/src/ObjectWriterWrapper.h",
        "nyxstone/src/Target/AArch64/MCTargetDesc/AArch64FixupKinds.h",
        "nyxstone/src/Target/AArch64/MCTargetDesc/AArch64MCExpr.h",
        "src/nyxstone_ffi.hpp",
    ];

    let sources = [
        "nyxstone/src/nyxstone.cpp",
        "nyxstone/src/ObjectWriterWrapper.cpp",
        "nyxstone/src/ELFStreamerWrapper.cpp",
        "src/nyxstone_ffi.cpp",
    ];

    // Tell cargo about NYXSTONE_LLVM_PREFIX
    println!("cargo:rerun-if-env-changed={}", ENV_LLVM_PREFIX);
    if let Ok(path) = env::var(ENV_LLVM_PREFIX) {
        println!("cargo:rerun-if-changed={}", path);
    }

    // Commented, because it is not currently needed, but might in the future:
    // The cxxbridge include dir is required to include auto generated c++ header files,
    // that contains shared data types defined in the rust part of the CXX bridge.

    // Generate path for CXX bridge generated files: project_name/target/cxxbridge
    // let out_dir = std::env::var("OUT_DIR").unwrap();
    // let cxxbridge_dir = out_dir + "/../../../../cxxbridge";

    let config = search_llvm_config();
    if config.is_err() {
        panic!("{} Please either install LLVM 15 with static libs into your PATH or supply the location via $NYXSTONE_LLVM_PREFIX", config.unwrap_err());
    };
    let config = config.unwrap();

    // Tell cargo about the library directory of llvm.
    let libdir = llvm_config(&config, ["--libdir"]);
    println!("cargo:libdir={}", libdir);
    println!("cargo:rustc-link-search=native={}", libdir);
    // Tell cargo about all llvm static libraries to link against.
    let llvm_libs = get_link_libraries(&config);
    for name in llvm_libs {
        println!("cargo:rustc-link-lib=static={}", name);
    }

    // llvm-sys uses the target os to select if system libs should be statically or dynamically linked.
    let system_linking = if target_os_is("musl") { "static" } else { "dylib" };

    // Tell cargo about the system libraries needed by llvm.
    for name in get_system_libraries(&config) {
        println!("cargo:rustc-link-lib={}={}", system_linking, name);
    }

    // Get the include directory for the c++ code.
    let llvm_include_dir = llvm_config(&config, ["--includedir"]);

    // Import Nyxstone C++ lib
    cxx_build::bridge("src/lib.rs")
        .flag_if_supported("-std=c++17")
        .include("nyxstone/include")
        .include(llvm_include_dir.trim())
        // .include(cxxbridge_dir)
        .files(sources)
        .compile("nyxstone_wrap");

    // Tell cargo about source deps
    for file in headers.iter().chain(sources.iter()) {
        println!("cargo:rerun-if-changed={}", file);
    }
}

/// Searches for LLVM in $NYXSTONE_LLVM_PREFIX/bin and in the path and ensures proper version and static
/// linking support.
///
/// # Returns
/// `Ok()` and PathBuf to llvm-config, `Err()` otherwise
fn search_llvm_config() -> Result<PathBuf> {
    let prefix = env::var(ENV_LLVM_PREFIX)
        .map(|p| PathBuf::from(p).join("bin"))
        .unwrap_or_else(|_| PathBuf::new());

    let llvm_config = Path::new(&prefix).join("llvm-config");
    let version = get_major_version(&llvm_config).map_err(|_| {
        anyhow!(
            "No llvm-config found in {}",
            if prefix == PathBuf::new() {
                "$PATH"
            } else {
                "$NYXSTONE_LLVM_PREFIX"
            }
        )
    })?;
    let can_link = can_link_static(&llvm_config);

    if version != "15" {
        return Err(anyhow!("LLVM major version is {}, must be 15.", version));
    }

    if !can_link {
        return Err(anyhow!("LLVM install does not include static libraries.",));
    }

    Ok(llvm_config)
}

// All following functions taken from llvm-sys crate and are licensed according to the following
// license:
// Copyright (c) 2015 Peter Marheine
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

fn llvm_config_ex<I, S>(binary: &Path, args: I) -> anyhow::Result<String>
where
    I: IntoIterator<Item = S>,
    S: AsRef<OsStr>,
{
    Ok(Command::new(binary)
        .args(args)
        .arg("--link-static") // Don't use dylib for >= 3.9
        .output()
        .and_then(|output| {
            if output.stdout.is_empty() {
                Err(io::Error::new(
                    io::ErrorKind::NotFound,
                    "llvm-config returned empty output",
                ))
            } else {
                Ok(String::from_utf8(output.stdout).expect("Output from llvm-config was not valid UTF-8"))
            }
        })?)
}

fn llvm_config<I, S>(binary: &Path, args: I) -> String
where
    I: IntoIterator<Item = S>,
    S: AsRef<OsStr>,
{
    llvm_config_ex(binary, args).expect("Surprising failure from llvm-config")
}

fn get_major_version(binary: &Path) -> Result<String> {
    Ok(llvm_config_ex(binary, ["--version"])?
        .split('.')
        .next()
        .ok_or(anyhow!("Unexpected llvm output"))?
        .to_owned())
}

fn can_link_static(binary: &Path) -> bool {
    llvm_config_ex(binary, ["--libs", "core", "mc", "all-targets"]).is_ok()
}

fn target_env_is(name: &str) -> bool {
    match env::var_os("CARGO_CFG_TARGET_ENV") {
        Some(s) => s == name,
        None => false,
    }
}

fn target_os_is(name: &str) -> bool {
    match env::var_os("CARGO_CFG_TARGET_OS") {
        Some(s) => s == name,
        None => false,
    }
}

fn target_dylib_extension() -> &'static str {
    if target_os_is("macos") {
        ".dylib"
    } else {
        ".so"
    }
}

fn get_system_libcpp() -> Option<&'static str> {
    if target_env_is("msvc") {
        // MSVC doesn't need an explicit one.
        None
    } else if target_os_is("macos") || target_os_is("freebsd") || target_env_is("musl") {
        // On OS X 10.9 and later, LLVM's libc++ is the default. On earlier
        // releases GCC's libstdc++ is default. Unfortunately we can't
        // reasonably detect which one we need (on older ones libc++ is
        // available and can be selected with -stdlib=lib++), so assume the
        // latest, at the cost of breaking the build on older OS releases
        // when LLVM was built against libstdc++.
        Some("c++")
    } else {
        // Otherwise assume GCC's libstdc++.
        // This assumption is probably wrong on some platforms, but would need
        // testing on them.
        Some("stdc++")
    }
}

/// Get the names of the dylibs required by LLVM, including the C++ standard
/// library.
fn get_system_libraries(llvm_config_path: &Path) -> Vec<String> {
    llvm_config(llvm_config_path, ["--system-libs"])
        .split(&[' ', '\n'] as &[char])
        .filter(|s| !s.is_empty())
        .map(|flag| {
            if target_env_is("msvc") {
                // Same as --libnames, foo.lib
                flag.strip_suffix(".lib")
                    .unwrap_or_else(|| panic!("system library '{}' does not appear to be a MSVC library file", flag))
            } else {
                if let Some(flag) = flag.strip_prefix("-l") {
                    // Linker flags style, -lfoo
                    if target_os_is("macos") {
                        // .tdb libraries are "text-based stub" files that provide lists of symbols,
                        // which refer to libraries shipped with a given system and aren't shipped
                        // as part of the corresponding SDK. They're named like the underlying
                        // library object, including the 'lib' prefix that we need to strip.
                        if let Some(flag) = flag.strip_prefix("lib").and_then(|flag| flag.strip_suffix(".tbd")) {
                            return flag;
                        }
                    }
                    return flag;
                }

                let maybe_lib = Path::new(flag);
                if maybe_lib.is_file() {
                    // Library on disk, likely an absolute path to a .so. We'll add its location to
                    // the library search path and specify the file as a link target.
                    println!("cargo:rustc-link-search={}", maybe_lib.parent().unwrap().display());

                    // Expect a file named something like libfoo.so, or with a version libfoo.so.1.
                    // Trim everything after and including the last .so and remove the leading 'lib'
                    let soname = maybe_lib
                        .file_name()
                        .unwrap()
                        .to_str()
                        .expect("Shared library path must be a valid string");
                    let (stem, _rest) = soname
                        .rsplit_once(target_dylib_extension())
                        .expect("Shared library should be a .so file");

                    stem.strip_prefix("lib")
                        .unwrap_or_else(|| panic!("system library '{}' does not have a 'lib' prefix", soname))
                } else {
                    panic!("Unable to parse result of llvm-config --system-libs: {}", flag)
                }
            }
        })
        .chain(get_system_libcpp())
        .map(str::to_owned)
        .collect()
}

fn get_link_libraries(llvm_config_path: &Path) -> Vec<String> {
    // Using --libnames in conjunction with --libdir is particularly important
    // for MSVC when LLVM is in a path with spaces, but it is generally less of
    // a hack than parsing linker flags output from --libs and --ldflags.

    fn get_link_libraries_impl(llvm_config_path: &Path) -> String {
        llvm_config(llvm_config_path, ["--libnames", "core", "mc", "all-targets"])
    }

    extract_library(&get_link_libraries_impl(llvm_config_path))
}

fn extract_library(s: &str) -> Vec<String> {
    s.split(&[' ', '\n'] as &[char])
        .filter(|s| !s.is_empty())
        .map(|name| {
            {
                // --libnames gives library filenames. Extract only the name that
                // we need to pass to the linker.
                // Match static library
                if let Some(name) = name.strip_prefix("lib").and_then(|name| name.strip_suffix(".a")) {
                    // Unix (Linux/Mac)
                    // libLLVMfoo.a
                    name
                } else if let Some(name) = name.strip_suffix(".lib") {
                    // Windows
                    // LLVMfoo.lib
                    name
                } else {
                    panic!("'{}' does not look like a static library name", name)
                }
            }
            .to_string()
        })
        .collect::<Vec<String>>()
}
