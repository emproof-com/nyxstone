use std::env;

use cmake_package::find_package;

const SEARCH_ENV_VAR_NAME: &str = "NYXSTONE_LLVM_PREFIX";
const CMAKE_SEARCH_NAME: &str = "CMAKE_PREFIX_PATH";

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

    // Exit early if we are in the docs.rs builder, since we do not need to build the c++ files or link against llvm.
    if std::env::var("DOCS_RS").is_ok() {
        return;
    }

    // Tell cargo about the relevant environment variables
    println!("cargo:rerun-if-env-changed={}", SEARCH_ENV_VAR_NAME);
    println!("cargo:rerun-if-env-changed={}", CMAKE_SEARCH_NAME);
    if let Ok(search) = env::var(SEARCH_ENV_VAR_NAME) {
        unsafe {
            if let Ok(current_search_paths) = env::var(CMAKE_SEARCH_NAME) {
                let separator = if cfg!(windows) { ";" } else { ":" };
                env::set_var(CMAKE_SEARCH_NAME, current_search_paths + separator + &search);
            } else {
                env::set_var(CMAKE_SEARCH_NAME, search);
            }
        }
    }

    // Get the include directory for the c++ code.
    const LLVM_VERSIONS: [&str; 5] = ["15", "16", "17", "18.0", "18.1"];
    let llvm = LLVM_VERSIONS
        .into_iter()
        // Always use the newest version available
        .rev()
        .map(|version| {
            find_package("LLVM")
                .components([
                    "core".into(),
                    "mc".into(),
                    "AllTargetsCodeGens".into(),
                    "AllTargetsAsmParsers".into(),
                    "AllTargetsDescs".into(),
                    "AllTargetsDisassemblers".into(),
                    "AllTargetsInfos".into(),
                    "AllTargetsMCAs".into(),
                ])
                .version(version)
                .find()
        })
        .find_map(Result::ok)
        .expect("Could not find LLVM with version 15-18");

    println!(
        "cargo:warning=LLVM version: {}",
        llvm.version.as_ref().expect("LLVM version was requested")
    );

    let llvm = llvm
        .target("LLVM")
        .expect("The target name LLVM was not found in the LLVM cmake package, please report a bug at https://github.com/emproof-com/nyxstone");

    println!("cargo:warning=LLVM libraries: {:?}", llvm.link_libraries);

    // Tell cargo about the libraries needed by LLVM.
    llvm.link();

    // Import Nyxstone C++ lib
    cxx_build::bridge("src/lib.rs")
        .std("c++17")
        .include("nyxstone/include")
        .include("nyxstone/vendor")
        .includes(llvm.include_directories)
        // .include(cxxbridge_dir)
        .files(sources)
        .compile("nyxstone_wrap");

    // Tell cargo about source deps
    for file in headers.iter().chain(sources.iter()) {
        println!("cargo:rerun-if-changed={}", file);
    }
}
