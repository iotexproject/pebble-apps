#!/usr/bin/env python3

import sys,os,binascii
import serial
import time
import serial.tools.list_ports
try:
    import transact
    import  configure as sensor
except ImportError:
    from python_tool import transact
    from python_tool import configure as sensor





selected_sensor  = sensor.DATA_CHANNEL_GPS|sensor.DATA_CHANNEL_SNR|sensor.DATA_CHANNEL_VBAT|sensor.DATA_CHANNEL_DEVINFO|sensor.DATA_CHANNEL_GAS|sensor.DATA_CHANNEL_TEMP|sensor.DATA_CHANNEL_PRESSURE|sensor.DATA_CHANNEL_HUMIDITY|sensor.DATA_CHANNEL_ENV_SENSOR|sensor.DATA_CHANNEL_TEMP2|sensor.DATA_CHANNEL_GYROSCOPE|sensor.DATA_CHANNEL_ACCELEROMETER|sensor.DATA_CHANNEL_CUSTOM_MOTION|sensor.DATA_CHANNEL_ACTION_SENSOR|sensor.DATA_CHANNEL_LIGHT_SENSOR
endpoint = "a11homvea4zo8t-ats.iot.us-east-1.amazonaws.com"
port = 8883
upload_period = 10
precise_gps = True
network = sensor.PEBBLE_CONTRACT_TEST_NET
aws_cert_file = './mqtt_cert/certificate.pem'
aws_key_file = './mqtt_cert/private.pem'
aws_root_key = './mqtt_cert/root.pem'




def findSerialPort():
    port_list = list(serial.tools.list_ports.comports())
    return port_list


def main():
    portList = findSerialPort()
    found_port = False
    com = serial.Serial()
    for i in portList:
        showStr = str(i[0])+" "+str(i[1])
        if "VID:PID=10C4:EA70" in i.hwid:
            if "Enhanced COM Port" in showStr:
                found_port = True
                com_port = i.device
    if not found_port:
        print("The device cannot be found, please connect the device via USB")
    else:
        cmd = transact.COM_CMD_DOWNLOAD_CONFIGURE
        comTrans = transact.COM_TRANSACT(com, com_port)
        comTrans.openPort()
        ch_str = hex(selected_sensor)[2:]
        if len(ch_str)& 1 == 1:
            ch_str= "0" + ch_str
        port_str = hex(port)[2:]
        if len(port_str)& 1 == 1:
            port_str= "0" + port_str
        period_str = hex(upload_period)[2:]
        if len(period_str)& 1 == 1:
            period_str= "0" + period_str
        trans_string = binascii.unhexlify(ch_str)[::-1].hex() + " " + endpoint +" " + binascii.unhexlify(port_str)[::-1].hex() + " " +  str(precise_gps) + " " + str(network) + " " + binascii.unhexlify(period_str)[::-1].hex()
        ret = comTrans.sendCommand(cmd, trans_string, len(trans_string))
        ret , bytePack = comTrans.receiveCommand(6)
        if ret:
            print("Parameters downloaded successfully")
        else:
            print("Parameter download failed")
        trans_string = ""
        with open(aws_cert_file,'r') as f:
            trans_string = f.read()
        cmd = transact.COM_CMD_DOWNLOAD_CERT
        comTrans.sendCommand(cmd, trans_string, len(trans_string))
        ret , bytePack = comTrans.receiveCommand(6)
        if ret:
            print("Cert downloaded successfully")
        else:
            print("Cert download failed")
        trans_string = ""
        with open(aws_key_file,'r') as f:
            trans_string = f.read()
        cmd = transact.COM_CMD_DOWNLOAD_KEY
        comTrans.sendCommand(cmd, trans_string, len(trans_string))
        ret , bytePack = comTrans.receiveCommand(6)
        if ret:
            print("Key downloaded successfully")
        else:
            print("Key download failed")
        trans_string = ""
        with open(aws_root_key,'r') as f:
            trans_string = f.read()
        cmd = transact.COM_CMD_DOWNLOAD_ROOT
        comTrans.sendCommand(cmd, trans_string, len(trans_string))
        ret , bytePack = comTrans.receiveCommand(6)
        if ret:
            print("Root downloaded successfully")
        else:
            print("Root download failed")
        comTrans.closePort()

if __name__ == '__main__':
    main()