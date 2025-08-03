# Llama LLM in C

Run an LLM on your GPU with Vulkan.  
This version has the power to run cmd.exe shell commands, but it will prompt you before it does so.

## Sources

- Downloaded model
  - ie. `vendor\models\Meta-Llama-3.1-8B-Instruct-Q5_K_M.gguf`

## Install

**NOTE:** You only need to do this if you don't want to use what is already committed under `build/`.

- Update the gitmodules in this repo
  ```
  git submodule update --init --recursive
  ```

- Setup MSFT vcpkg (for libcurl)
  ```
  cd vendor/vcpkg
  bootstrap-vcpkg.bat
  vcpkg.exe install curl:x64-windows
  ```

- Download and extract [`w64devkit`](https://github.com/skeeto/w64devkit/releases).
  - ie. `vendor\w64devkit\w64devkit.exe`
- Download and install the [`Vulkan SDK`](https://vulkan.lunarg.com/sdk/home#windows) with the default settings.
  - ie. `vulkansdk-windows-X64-1.4.321.1.exe` (244MB)

- Launch `w64devkit.exe` and run the following commands to copy Vulkan dependencies:
  ```sh
  cp -r vendor/vcpkg/packages/curl_x64-windows/bin/* $W64DEVKIT_HOME/bin/
  cp -r vendor/vcpkg/packages/curl_x64-windows/lib/* $W64DEVKIT_HOME/lib/
  cp -r vendor/vcpkg/packages/curl_x64-windows/include/* $W64DEVKIT_HOME/include/
  SDK_VERSION=1.4.321.1
  cp /VulkanSDK/$SDK_VERSION/Bin/glslc.exe $W64DEVKIT_HOME/bin/
  cp /VulkanSDK/$SDK_VERSION/Lib/vulkan-1.lib $W64DEVKIT_HOME/lib/
  cp -r /VulkanSDK/$SDK_VERSION/Include/* $W64DEVKIT_HOME/include/
  cat > $W64DEVKIT_HOME/lib/pkgconfig/vulkan.pc <<EOF
  Name: Vulkan-Loader
  Description: Vulkan Loader
  Version: $SDK_VERSION
  Libs: -lvulkan-1
  EOF
  ```

- Build llama.cpp lib
  ```sh
  cd vendor/llama.cpp
  cmake -B build -DGGML_VULKAN=ON -DBUILD_SHARED_LIBS=ON
  cmake --build build --config Release -j 32
  ```

- Try the llama.cpp cli
  ```
  cd vendor\llama.cpp\build\bin\Release\
  llama-simple-chat.exe -m ../../../../models/Meta-Llama-3.1-8B-Instruct-Q5_K_M.gguf
  ```

## Compile
  ```
  build.bat
  ```

## Use
```
  build\main.exe
```
