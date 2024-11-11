## Build your adapter

This adapter is designed to works with a D1 Mini (ESP8266) but it provides convenient header pins to use it with any other uController.

It is designed by Palazzetti using a Si8621 (Silicon Labs Isolator) to provide electrical isolation between uController and Stove electronic.  
This design uses exact same schematic and components.

### Schematic

All files are inside `schematic` subfolder and has been designed with KiCad (free and open source)  

![WPalaControl schematic](img/schematic.png)

### PCB

<img src="img/pcb-top.png" alt="WPalaControl PCB Top" width="239" height="315"><img src="img/pcb-bottom.png" alt="WPalaControl PCB Bottom" width="239" height="315">  

<img src="img/3boards.png" alt="WPalaControl 3boards"  width="334">  

*I'm producing small batches of this adapter.*  
*If you are interested, you can find it here: [WPalaControl on tindie](https://www.tindie.com/products/35770/)* 🛍️

### Box

Box project (Fusion 360 & STL) can be found into `box` folder

![WPalaControl box](img/box.png)

### Firmware

Source code can be compiled using VSCode/PlatformIO and flashed onto a D1 Mini  
Or  
Download latest release in [Release section](https://github.com/Domochip/WPalaControl/releases)
