use anyhow::{anyhow, ensure, Context, Result};
use std::env;
use std::ffi::OsStr;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};

const ENV_LLVM_PREFIX: &str = "NYXSTONE_LLVM_PREFIX";
const ENV_FORCE_FFI_LINKING: &str = "NYXSTONE_LINK_FFI";

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

    // Exit early if we are in the docs.rs builder, since we do not need to build the c++ files or link against llvm.
    if std::env::var("DOCS_RS").is_ok() {
        return;
    }

    // Commented, because it is not currently needed, but might in the future:
    // The cxxbridge include dir is required to include auto generated c++ header files,
    // that contains shared data types defined in the rust part of the CXX bridge.

    // Generate path for CXX bridge generated files: project_name/target/cxxbridge
    // let out_dir = std::env::var("OUT_DIR").unwrap();
    // let cxxbridge_dir = out_dir + "/../../../../cxxbridge";

    // === The following code is adapted from llvm-sys, see below for licensing ===
    let llvm_config_path = match search_llvm_config() {
        Ok(config) => config,
        Err(e) => panic!("{e} Please either install LLVM version >= 15 into your PATH or supply the location via $NYXSTONE_LLVM_PREFIX"),
    };

    // Tell cargo about the library directory of llvm.
    let libdir = llvm_config(&llvm_config_path, ["--libdir"]);

    // Export information to other crates
    println!("cargo:config_path={}", llvm_config_path.display()); // will be DEP_LLVM_CONFIG_PATH
    println!("cargo:libdir={}", libdir); // DEP_LLVM_LIBDIR

    // Link LLVM libraries
    println!("cargo:rustc-link-search=native={}", libdir);
    for link_search_dir in get_system_library_dirs() {
        println!("cargo:rustc-link-search=native={}", link_search_dir);
    }
    // We need to take note of what kind of libraries we linked to, so that
    // we can link to the same kind of system libraries
    let (kind, libs) = get_link_libraries(&llvm_config_path);
    for name in libs {
        println!("cargo:rustc-link-lib={}={}", kind.string(), name);
    }

    // Link system libraries
    // We get the system libraries based on the kind of LLVM libraries we link to, but we link to
    // system libs based on the target environment.
    let sys_lib_kind = LibraryKind::Dynamic;
    for name in get_system_libraries(&llvm_config_path, kind) {
        println!("cargo:rustc-link-lib={}={}", sys_lib_kind.string(), name);
    }

    if target_env_is("msvc") && is_llvm_debug(&llvm_config_path) {
        println!("cargo:rustc-link-lib=msvcrtd");
    }

    // Sometimes, llvm-config might not report that ffi is needed as a system library.
    // There is no way to detect this, thus the user must notify us via the environment
    // variable.
    if env::var(ENV_FORCE_FFI_LINKING).is_ok() {
        println!("cargo:rustc-link-lib=dylib=ffi");
    }

    // ====================================================

    // Get the include directory for the c++ code.
    let llvm_include_dir = llvm_config(&llvm_config_path, ["--includedir"]);

    // Import Nyxstone C++ lib
    cxx_build::bridge("src/lib.rs")
        .std("c++17")
        .include("nyxstone/include")
        .include("nyxstone/vendor")
        .include(llvm_include_dir.trim())
        // .include(cxxbridge_dir)
        .files(sources)
        .compile("nyxstone_wrap");

    // Tell cargo about source deps
    for file in headers.iter().chain(sources.iter()) {
        println!("cargo:rerun-if-changed={}", file);
    }
}

/// Searches for LLVM in $NYXSTONE_LLVM_PREFIX/bin and in the path and ensures proper version
/// # Returns
/// `Ok()` and PathBuf to llvm-config, `Err()` otherwise
fn search_llvm_config() -> Result<PathBuf> {
    let prefix = env::var(ENV_LLVM_PREFIX)
        .and_then(|p| {
            if p.is_empty() {
                return Err(std::env::VarError::NotPresent);
            }

            Ok(PathBuf::from(p).join("bin"))
        })
        .unwrap_or_else(|_| PathBuf::new());

    for name in llvm_config_binary_names() {
        let llvm_config = Path::new(&prefix).join(name);

        let Ok(version) = get_major_version(&llvm_config) else {
            continue;
        };

        let version = version.parse::<u32>().context("Parsing LLVM version")?;

        ensure!(
            (15..=18).contains(&version),
            "LLVM major version is {}, must be 15-18.",
            version
        );

        return Ok(llvm_config);
    }

    Err(anyhow!(
        "No llvm-config found in {}",
        if prefix == PathBuf::new() {
            "$PATH"
        } else {
            "$NYXSTONE_LLVM_PREFIX"
        }
    ))
}

fn get_major_version(binary: &Path) -> Result<String> {
    Ok(llvm_config_ex(binary, ["--version"])
        .context("Extracting LLVM major version")?
        .split('.')
        .next()
        .expect("Unexpected llvm-config output.")
        .to_owned())
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

/// Return an iterator over possible names for the llvm-config binary.
fn llvm_config_binary_names() -> impl Iterator<Item = String> {
    let base_names = (15..=18)
        .flat_map(|version| {
            [
                format!("llvm-config-{}", version),
                format!("llvm-config{}", version),
                format!("llvm{}-config", version),
            ]
        })
        .chain(["llvm-config".into()])
        .collect::<Vec<_>>();

    // On Windows, also search for llvm-config.exe
    if target_os_is("windows") {
        IntoIterator::into_iter(base_names)
            .flat_map(|name| [format!("{}.exe", name), name])
            .collect::<Vec<_>>()
    } else {
        base_names.to_vec()
    }
    .into_iter()
}

/// Invoke the specified binary as llvm-config.
fn llvm_config<I, S>(binary: &Path, args: I) -> String
where
    I: IntoIterator<Item = S>,
    S: AsRef<OsStr>,
{
    llvm_config_ex(binary, args).expect("Surprising failure from llvm-config")
}

/// Invoke the specified binary as llvm-config.
///
/// Explicit version of the `llvm_config` function that bubbles errors
/// up.
fn llvm_config_ex<I, S>(binary: &Path, args: I) -> anyhow::Result<String>
where
    I: IntoIterator<Item = S>,
    S: AsRef<OsStr>,
{
    let mut cmd = Command::new(binary);
    (|| {
        let Output { status, stdout, stderr } = cmd.args(args).output()?;
        let stdout = String::from_utf8(stdout).context("stdout")?;
        let stderr = String::from_utf8(stderr).context("stderr")?;
        if status.success() {
            Ok(stdout)
        } else {
            Err(anyhow::anyhow!(
                "status={status}\nstdout={}\nstderr={}",
                stdout.trim(),
                stderr.trim()
            ))
        }
    })()
    .with_context(|| format!("{cmd:?}"))
}

/// Get the names of the dylibs required by LLVM, including the C++ standard
/// library.
fn get_system_libraries(llvm_config_path: &Path, kind: LibraryKind) -> Vec<String> {
    let link_arg = match kind {
        LibraryKind::Static => "--link-static",
        LibraryKind::Dynamic => "--link-shared",
    };

    llvm_config(llvm_config_path, ["--system-libs", link_arg])
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

                    if let Some(i) = flag.find(".so.") {
                        // On some distributions (OpenBSD, perhaps others), we get sonames
                        // like "-lz.so.7.0". Correct those by pruning the file extension
                        // and library version.
                        return &flag[..i];
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

/// Return additional linker search paths that should be used but that are not discovered
/// by other means.
///
/// In particular, this should include only directories that are known from platform-specific
/// knowledge that aren't otherwise discovered from either `llvm-config` or a linked library
/// that includes an absolute path.
fn get_system_library_dirs() -> impl IntoIterator<Item = &'static str> {
    if target_os_is("openbsd") {
        Some("/usr/local/lib")
    } else {
        None
    }
}

fn target_dylib_extension() -> &'static str {
    if target_os_is("macos") {
        ".dylib"
    } else {
        ".so"
    }
}

/// Get the library that must be linked for C++, if any.
fn get_system_libcpp() -> Option<&'static str> {
    if target_env_is("msvc") {
        // MSVC doesn't need an explicit one.
        None
    } else if target_os_is("macos") {
        // On OS X 10.9 and later, LLVM's libc++ is the default. On earlier
        // releases GCC's libstdc++ is default. Unfortunately we can't
        // reasonably detect which one we need (on older ones libc++ is
        // available and can be selected with -stdlib=lib++), so assume the
        // latest, at the cost of breaking the build on older OS releases
        // when LLVM was built against libstdc++.
        Some("c++")
    } else if target_os_is("freebsd") || target_os_is("openbsd") {
        Some("c++")
    } else if target_env_is("musl") {
        // The one built with musl.
        Some("c++")
    } else {
        // Otherwise assume GCC's libstdc++.
        // This assumption is probably wrong on some platforms, but would need
        // testing on them.
        Some("stdc++")
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum LibraryKind {
    Static,
    Dynamic,
}

impl LibraryKind {
    pub fn string(&self) -> &'static str {
        match self {
            LibraryKind::Static => "static",
            LibraryKind::Dynamic => "dylib",
        }
    }
}

/// Get the names of libraries to link against, along with whether it is static or shared library.
fn get_link_libraries(llvm_config_path: &Path) -> (LibraryKind, Vec<String>) {
    // Using --libnames in conjunction with --libdir is particularly important
    // for MSVC when LLVM is in a path with spaces, but it is generally less of
    // a hack than parsing linker flags output from --libs and --ldflags.

    fn get_link_libraries_impl(llvm_config_path: &Path, kind: LibraryKind) -> anyhow::Result<String> {
        // Windows targets don't get dynamic support.
        // See: https://gitlab.com/taricorp/llvm-sys.rs/-/merge_requests/31#note_1306397918
        if target_env_is("msvc") && kind == LibraryKind::Dynamic {
            anyhow::bail!("Dynamic linking to LLVM is not supported on Windows");
        }

        let link_arg = match kind {
            LibraryKind::Static => "--link-static",
            LibraryKind::Dynamic => "--link-shared",
        };
        llvm_config_ex(llvm_config_path, ["--libnames", link_arg])
    }

    // Prefer static linking
    let preferences = [LibraryKind::Static, LibraryKind::Dynamic];

    for kind in preferences {
        match get_link_libraries_impl(llvm_config_path, kind) {
            Ok(s) => return (kind, extract_library(&s, kind)),
            Err(err) => {
                println!("failed to get {} libraries from llvm-config: {err:?}", kind.string())
            }
        }
    }

    panic!("failed to get linking libraries from llvm-config",);
}

fn extract_library(s: &str, kind: LibraryKind) -> Vec<String> {
    s.split(&[' ', '\n'] as &[char])
        .filter(|s| !s.is_empty())
        .map(|name| {
            // --libnames gives library filenames. Extract only the name that
            // we need to pass to the linker.
            match kind {
                LibraryKind::Static => {
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
                LibraryKind::Dynamic => {
                    // Match shared library
                    if let Some(name) = name.strip_prefix("lib").and_then(|name| name.strip_suffix(".dylib")) {
                        // Mac
                        // libLLVMfoo.dylib
                        name
                    } else if let Some(name) = name.strip_prefix("lib").and_then(|name| name.strip_suffix(".so")) {
                        // Linux
                        // libLLVMfoo.so
                        name
                    } else if let Some(name) =
                        IntoIterator::into_iter([".dll", ".lib"]).find_map(|suffix| name.strip_suffix(suffix))
                    {
                        // Windows
                        // LLVMfoo.{dll,lib}
                        name
                    } else {
                        panic!("'{}' does not look like a shared library name", name)
                    }
                }
            }
            .to_string()
        })
        .collect::<Vec<String>>()
}

fn is_llvm_debug(llvm_config_path: &Path) -> bool {
    // Has to be either Debug or Release
    llvm_config(llvm_config_path, ["--build-mode"]).contains("Debug")
}
