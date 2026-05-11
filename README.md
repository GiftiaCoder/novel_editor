# novel_editor

this is a simple web novel editor writen by AI `@DeepSeek V4`, `@GPT-5.5 Thinking`

```bash
# build
mkdir build
cd build
cmake ..
make -j 8

# run
cd ..
build/src/novel_editor \
    --content_root=./data \
    --auth_username=admin \
    --auth_password=admin \
    --novel_name='a boring story'
```