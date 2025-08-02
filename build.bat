@echo on
clang ^
  @clang_options.rsp ^
  -Ivendor/llama.cpp/include ^
  -Ivendor/llama.cpp/ggml/include ^
  -Lvendor\llama.cpp\build\src\Release -lllama.lib ^
  -Lvendor\llama.cpp\build\ggml\src\Release -lggml-base.lib ^
  -Lvendor\llama.cpp\build\ggml\src\Release -lggml.lib ^
  src/main.c ^
  -o build/main.exe