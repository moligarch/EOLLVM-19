# EOLLVM19

**EOLLVM19** (Enhanced OLLVM for LLVM 19) is a modernized and extended obfuscation suite for LLVM 19.1.x.

Building upon the foundation of [OLLVM-4](https://github.com/obfuscator-llvm/obfuscator) and [OLLVM 16](https://github.com/wwh1004/ollvm-16), this project not only ports the classic obfuscation passes to the New Pass Manager (NPM) but also introduces a **String Encryption** mechanism, making it a complete solution for protecting native code.

## Features

- **String Encryption** (`-mllvm -sobf`): **[New]** Encrypts hardcoded string literals.
    - **Static Mode** (Default): Decrypts at startup.
    - **Stack Mode** (`-sobf_mode=stack`): Decrypts on stack at runtime for maximum security.
- **Control Flow Flattening** (`-mllvm -fla`): Flattens the CFG to hide logic structure.
- **Bogus Control Flow** (`-mllvm -bcf`): Adds fake blocks and branches to confuse decompilers.
- **Instruction Substitution** (`-mllvm -sub`): Replaces standard binary operators with complex, mathematically equivalent sequences.

## Build Instructions (Windows)

Follow these steps to build the obfuscator from scratch using the Command Line and Visual Studio 2022.

### Prerequisites
- **Git**
- **CMake** (3.20+)
- **Visual Studio 2022** (with C++ Desktop Development workload)
- **Windows SDK version** (`10.0.19041.0`)
- **Python 3** (required by LLVM build system)

### Step-by-Step Build

Run the following commands in your Command Prompt (cmd.exe) or PowerShell.

**1. Create the Workspace**
Create a clean directory for the project.
```cmd
mkdir eollvm19
cd eollvm19
```

**2. Clone LLVM 19.1.3**
Clone the specific tag of LLVM (Shallow clone to save space/time).
```cmd
git clone -b llvmorg-19.1.3 --depth 1 https://github.com/llvm/llvm-project.git llvm-19.1.3
```

**3. Clone EOLLVM19**
Clone this repository containing the obfuscation source code.
```cmd
git clone https://github.com/moligarch/eollvm-19 eollvm
```

**4. Copy Obfuscation Files**
Copy the enhanced `llvm` directory into the official LLVM source tree.
```cmd
xcopy /E /Y /I eollvm\llvm llvm-19.1.3\llvm
```

**5. Apply the Patch**
Apply the patch to register the new passes in the build system and `PassBuilder`.
```cmd
cd llvm-19.1.3
git apply ..\eollvm\EnableEOLLVM19.1.3.patch
```

**6. Create Build Directory**
Move back to the root and create a build folder.
```cmd
cd ..
mkdir build
cd build
```

**7. Generate Solution with CMake**
Run CMake to generate the Visual Studio solution.
```cmd
cmake -S ..\llvm-19.1.3\llvm -B . -G "Visual Studio 17 2022" ^
        -A x64 -Thost=x64 -DLLVM_ENABLE_PROJECTS="clang;lld" ^
        -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=X86 ^
        -DLLVM_INCLUDE_TESTS=OFF ^
        -DLLVM_INCLUDE_EXAMPLES=OFF ^
        -DLLVM_INCLUDE_BENCHMARKS=OFF ^
        -DLLVM_BUILD_DOCS=OFF ^
        -DLLVM_OBFUSCATION_LINK_INTO_TOOLS=ON ^
        -DLLVM_OPTIMIZED_TABLEGEN=ON ^
        -DLLVM_ENABLE_BINDINGS=OFF
```

**8. Build the Project**
You can now build directly from the command line (this takes time):
```cmd
cmake --build . --config Release --target clang
```
*Alternatively, you can open `LLVM.sln` generated in the `build` folder with Visual Studio and build the `clang` target manually. (**Recommended: Release Configuration**)*

---

## Integration with Visual Studio 2022

Once built, you can use your new compiler in other Visual Studio projects to obfuscate your C++ code.

1.  **Open your Target Project** in Visual Studio.
2.  **Properties** -> **Configuration Properties** -> **General** -> **Platform Toolset**.
    * Change this to **LLVM (clang-cl)**.
    * *Note: You must have "C++ Clang tools for Windows" installed via the VS Installer.*
3.  **Tell VS to use YOUR Clang:**
    * You may need to add your `eollvm19\build\Release\bin` folder to your System `PATH` so VS finds your custom compiler instead of the default one.(**Recommended** to set `LLVM_INSTALL_DIR` as `<path\to\eollvm19\build\Release>)
    * Alternatively, create a `Directory.Build.props` your `.sln` file with below content:
    ```xml
    <Project>
        <PropertyGroup>
            <LLVMInstallDir>$(LLVM_INSTALL_DIR)</LLVMInstallDir>
            <LLVMToolsVersion>19.1.3</LLVMToolsVersion>
        </PropertyGroup>
    </Project>
    ```
4.  **Add Obfuscation Flags:**
    * Go to **Properties** -> **C/C++** -> **Command Line**.
    * In **Additional Options**, add your desired flags:
        ```text
        -mllvm -fla -mllvm -sub -mllvm -bcf -mllvm -sobf
        ```

## Troubleshooting

### "Stack Overflow" / C1001 Error
Obfuscation (especially Substitution with high loops) requires deep recursion. If the compiler crashes:

1.  Open **Developer Command Prompt for VS 2022**.
2.  Navigate to your build bin folder: `cd eollvm19\build\Release\bin`
3.  Increase the stack reserve size for the compiler:
    ```cmd
    editbin /STACK:16777216 clang-cl.exe
    editbin /STACK:16777216 clang.exe
    ```