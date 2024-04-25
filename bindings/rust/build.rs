use anyhow::{anyhow, Result};
use std::env;
use std::ffi::OsStr;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;

const ENV_LLVM_PREFIX: &str = "LLVM_SYS_150_PREFIX";

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

    // Commented, because it is not currently needed, but might in the future:
    // The cxxbridge include dir is required to include auto generated c++ header files,
    // that contains shared data types defined in the rust part of the CXX bridge.

    // Generate path for CXX bridge generated files: project_name/target/cxxbridge
    // let out_dir = std::env::var("OUT_DIR").unwrap();
    // let cxxbridge_dir = out_dir + "/../../../../cxxbridge";

    let config = match search_llvm_config() {
        Ok(config) => config,
        Err(e) => panic!("{e} Please either install LLVM 15 with static libs into your PATH or supply the location via $NYXSTONE_LLVM_PREFIX"),
    };

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

/// Searches for LLVM in $NYXSTONE_LLVM_PREFIX/bin and in the path and ensures proper version.
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
                ENV_LLVM_PREFIX
            }
        )
    })?;

    if version != "15" {
        return Err(anyhow!("LLVM major version is {}, must be 15.", version));
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
