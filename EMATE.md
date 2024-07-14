To use the integrated firmware update, you will need to flash the firmware UF2 file manually first via USB. Then, install the `emate-fw` package and the `emate-kbd` package.

The `update-emate-fw` utility is for installing packaged firmware files from `/usr/lib/emate-firmware`. See below for building and flashing custom firmware.

## Custom firmware

You can modify firmware in the `emate` branch of `beepyberry-rp2040`,

```
mkdir build
cd build
make -DPICO_BOARD=emate ..
make
```

This will produce files in `build`, the `i2c_puppet.uf2` flashable over USB.
Also, there will be a file `app/firmware.hex`. This is the actual firmware payload minus the updater stub. It needs to be fixed up before flashing via the integrated updater, by converting line endings and prepending a header to the file.

```
dos2unix firmware.hex
sed -i '1i +emate' firmware.hex
cat firmware.hex | sudo tee /sys/firmware/emate/fw_update
```

If the firmware doesn't work or is otherwise unresponsive, you'll have to reflash the complete UF2 firmware over USB.
