[EN](README.md)|[KR](README.ko.md)

## BDSX-CORE
![logo](icon.png)  
It's the core for [bdsx](https://github.com/karikera/bdsx)

## Build It Self
* Requirement
[Visual Studio 2019](https://visualstudio.microsoft.com/)  
[Visual Studio Code](https://code.visualstudio.com/)  
[NASM](https://www.nasm.us/) & Set PATH - required by node-chakracore  
[Python2.x](https://www.python.org/downloads/release/python-2718/) & Set PATH - required by node-chakracore  

1. Clone bdsx-core, elementminus, ken
**[parent directory]**  
├ ken (https://github.com/karikera/ken) - personal library project  
├ elementminus (https://github.com/karikera/elementminus) - dll injecter for BDS  
└ bdsx-core (https://github.com/karikera/bdsx-core)  

2. Update git submodules.

3. Build bdsx.sln with Visual Studio 2019.

## Release Build Outputs
* [parent directory]/bdsx-core/release/bdsx-core.zip