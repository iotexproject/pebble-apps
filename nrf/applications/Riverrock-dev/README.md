# Riverrock-dev

A branch of riverrock that supports downloading pebble parameters via usb.

## usage

1. Start pebble and go to the "About Pebble" menu

2. Modify the parameters in python_tool/update.py

   ```
   selected_sensor  = sensor.DATA_CHANNEL_GPS|sensor.DATA_CHANNEL_SNR|sensor.DATA_CHANNEL_VBAT|sensor.DATA_CHANNEL_DEVINFO|sensor.DATA_CHANNEL_GAS|sensor.DATA_CHANNEL_TEMP|sensor.DATA_CHANNEL_PRESSURE|sensor.DATA_CHANNEL_HUMIDITY|sensor.DATA_CHANNEL_ENV_SENSOR|sensor.DATA_CHANNEL_TEMP2|sensor.DATA_CHANNEL_GYROSCOPE|sensor.DATA_CHANNEL_ACCELEROMETER|sensor.DATA_CHANNEL_CUSTOM_MOTION|sensor.DATA_CHANNEL_ACTION_SENSOR|sensor.DATA_CHANNEL_LIGHT_SENSOR
   endpoint = "a11homvea4zo8t-ats.iot.us-east-1.amazonaws.com"
   port = 8883
   precise_gps = True
   network = sensor.PEBBLE_CONTRACT_TEST_NET
   aws_cert_file = './mqtt_cert/certificate.pem'
   aws_key_file = './mqtt_cert/private.pem'
   aws_root_key = './mqtt_cert/root.pem'
   ```

​		*Make sure you are using the correct AWS credentials*

3. Make sure python3 is installed and run the following commands in the python_tool directory

   ```
   pip install -r requirements.txt
   python  update.py
   ```

   

4. See the following log indicating that the parameters were downloaded successfully

   ```
   Cert downloaded successfully
   Key downloaded successfully
   Root downloaded successfully
   ```
