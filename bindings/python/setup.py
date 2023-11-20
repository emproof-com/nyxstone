from glob import glob
from setuptools import setup
import os
import subprocess

try:
    from pybind11.setup_helpers import Pybind11Extension
except ImportError:
    from setuptools import Extension as Pybind11Extension

LLVM_PREFIX_NAME = "NYXSTONE_LLVM_PREFIX"


class ValidLLVMConfig:
    def __init__(self):
        llvm_config = (
            os.path.join(os.getenv(LLVM_PREFIX_NAME), "bin/llvm-config")
            if os.getenv(LLVM_PREFIX_NAME) != None
            else "llvm-config"
        )

        try:
            version = subprocess.run(
                [llvm_config, "--version"], capture_output=True
            ).stdout.decode("utf-8")
        except FileNotFoundError as e:
            print(f"Could not find llvm-config in ${LLVM_PREFIX_NAME} or $PATH")
            exit(1)

        major_version = version.split(".")[0]

        if major_version != "15":
            print(
                f"LLVM Major version must be 15, found {major_version}! Try setting the env variable ${LLVM_PREFIX_NAME} to tell nyxstone about the install location of LLVM 15."
            )
            exit(1)

        lib_output = subprocess.run(
            [
                llvm_config,
                "--link-static",
                "--libs",
            ],
            capture_output=True,
        )

        if lib_output.returncode != 0:
            print(
                "Cannot link statically, please install LLVM with static linking support"
            )
            exit(1)

        self.config = llvm_config

    def get_llvm_libraries(self) -> (list[str], str):
        lib_output = subprocess.run(
            [
                self.config,
                "--link-static",
                "--system-libs",
                "--libs",
                "core",
                "mc",
                "all-targets",
            ],
            capture_output=True,
        )

        libraries = (
            lib_output.stdout.decode("utf-8")
            .replace("\n", " ")
            .strip()
            .replace("-l", "")
            .split(" ")
        )

        lib_location = (
            subprocess.run([self.config, "--ldflags"], capture_output=True)
            .stdout.decode("utf-8")
            .replace("\n", "")  # remove trailing '\n'
            .replace("-L", "")  # remove leading -L re-added by pybind
            .strip()  # strip trailing whitespace
        )

        return libraries, lib_location

    def get_llvm_include_dir(self) -> str:
        llvm_include_dir = (
            subprocess.run([self.config, "--includedir"], capture_output=True)
            .stdout.decode("utf-8")
            .replace("\n", "")
            .strip()
        )

        return llvm_include_dir


llvm = ValidLLVMConfig()
llvm_libs, llvm_lib_dir = llvm.get_llvm_libraries()
llvm_inc_dir = llvm.get_llvm_include_dir()

nyxstone_srcs = glob("nyxstone-cpp/src/*.cpp")

binder = ["nyxstone-cpp/pynyxstone.cpp"]

srcs = sorted(nyxstone_srcs + binder)

desc = "Python bindings for the nyxstone library."

ext_modules = [
    Pybind11Extension(
        name="nyxstone_cpp",
        sources=srcs,
        include_dirs=["nyxstone-cpp/include/", "nyxstone-cpp/src/", llvm_inc_dir],
        libraries=llvm_libs,
        library_dirs=[llvm_lib_dir],
        extra_link_args=[
            "-static-libstdc++",
            "-static-libgcc",
        ],  # try to reduce dependencies
    )
]

setup(
    name="nyxstone",
    version="0.1.0",
    description=desc,
    author="emproof B.V.",
    author_email="oss@emproof.com",
    packages=["nyxstone"],
    ext_modules=ext_modules,
)
