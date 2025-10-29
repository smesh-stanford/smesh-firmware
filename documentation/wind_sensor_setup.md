# Wind anemometer + direction - Firmware Integration Meshtastic
## Materials required for wind sensing
We are concerned with 
materials required for 3D paws: https://3dpaws.comet.ucar.edu/data-loggers/raspberry-pi

### Anemometer 
- SS451A: https://www.digikey.com/en/products/detail/SS451A/480-3587-ND/2505488?curr=usd&utm_campaign=buynow&utm_medium=aggregator&utm_source=octopart
	- Needs to be configured to a GPIO with interrupts
### wind vane
- AS5600: https://www.digikey.com/en/products/detail/ams-osram-usa-inc/AS5600-SO-EK-AB/5066879
	- Communicates over I2C

## Steps already taken towards a solution
Raspberry Pi code for wind sensing direction/speed from 3D PAWS:
- https://github.com/3d-paws/3D-PAWS-Raspberry-Pi/blob/main/scripts/sensors/wind_direction.py
- https://github.com/3d-paws/3D-PAWS-Raspberry-Pi/blob/main/scripts/sensors/wind_speed.py

Our attempt at wind sensing/speed:
*Software*
- as5600: https://github.com/smesh-stanford/smesh/blob/wind_sensor/snode/wind_sensor/as5600-log.py
- GPIO pin detection code for AS5600: https://github.com/smesh-stanford/smesh/blob/wind_sensor/snode/wind_sensor/anemometer_test.py

*Documentation for electrical*
https://docs.google.com/document/d/1-gOGSjNiH_r-fHjzeMtcpTH4H5bGbSGP-S1RJYpoTKc/edit?tab=t.0

## Meshtastic steps to take
Method of implementing anemometer:
- We can use a GPIO on the meshtastic and use IRAM to establish a hardware interrupt which occurs on the RAM (preventing a crash if we are writing to Flash when we get an interrupt)
