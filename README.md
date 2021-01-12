[EN](README.md)|[KR](README.ko.md)

## BDSX-CORE
![logo](icon.png)  
It's the core for [bdsx](https://github.com/karikera/bdsx)

## Build It Self
* Requirement
[Visual Studio 2019](https://visualstudio.microsoft.com/)  
[Visual Studio Code](https://code.visualstudio.com/)  
[NASM](https://www.nasm.us/) & Set PATH - It's needed by node-chakracore  
[Python2.x](https://www.python.org/downloads/release/python-2718/) & Set PATH - It's needed by node-chakracore  

1. Clone bdsx-core, bdsx, ken(ken is a personal library project)  
**[parent directory]**  
├ ken (https://github.com/karikera/ken)  
├ bdsx-core (https://github.com/karikera/bdsx-core)  
└ bdsx (https://github.com/karikera/bdsx)

2. Update git submodules.

3. Build bdsx.sln with Visual Studio 2019.

## Outputs
* [parent directory]/bdsx/bedrock_server/ChakraCore.dll
* [parent directory]/bdsx/bedrock_server/Chakra.dll
* [parent directory]/bdsx/mods/bdsx.dll
* [parent directory]/bdsx/mods/bdsx.pdb
