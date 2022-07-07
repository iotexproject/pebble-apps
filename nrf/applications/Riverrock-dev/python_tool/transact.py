import sys, os
import serial
#from enum import Enum, auto
import time

# command  define


COM_CMD_DOWNLOAD_CONFIGURE = 0x2F
COM_CMD_DOWNLOAD_CERT  = 0x30
COM_CMD_DOWNLOAD_KEY = 0x31
COM_CMD_DOWNLOAD_ROOT = 0x32


COM_ACK_SUCCESS  =  0

class COM_TRANSACT:
    def __init__(self, com, port):
        self.comSerial = com
        self.comPort = port

    def calcCRC16(self, buf, size): 
        crc = 0x0000
        for i in range(0,size):
            crc ^= (buf[i]<<8)
            for j in range(0,8):
                if crc & 0x8000:
                    crc = (crc<<1) ^ 0x8005
                else:
                    crc = (crc<<1)
        return ((crc & 0xFF00)>>8, crc&0x00FF)
    
    def openPort(self):
        if not self.comSerial.is_open:
            try:
                self.comSerial.baudrate = 1000000
                self.comSerial.port = self.comPort
                self.comSerial.bytesize = 8
                self.comSerial.parity = 'N'
                self.comSerial.stopbits = 1.0
                self.comSerial.timeout = 1
                self.comSerial.rts = False
                self.comSerial.dtr = False
                self.comSerial.open()
                return True
            except Exception as e:
                self.comSerial.close()
                return False

    def closePort(self):
        if self.comSerial.is_open:
            try:
                self.comSerial.close()
            except Exception as e:
                print(e)

    def sendCommand(self, cmd, dat, size):
        try:
            tmpBuf = bytearray(b'\xAA')
            tmpBuf.append(cmd)
            if size:
                tmpBuf.append((size & 0xFF00) >> 8)
                tmpBuf.append(size & 0x00FF)
                val = dat.encode()
                sendBuf = tmpBuf + val
            else:
                tmpBuf.append(0x00)
                tmpBuf.append(0x00)
                sendBuf = tmpBuf
            crcH, crcL = self.calcCRC16(sendBuf[1:], len(sendBuf)-1)
            sendBuf.append(crcH)
            sendBuf.append(crcL)
            if self.comSerial.is_open:
                self.comSerial.write(sendBuf)
                #self.comSerial.write(b'123')
                #self.closePort()
                return True
            else:
                return False
        except Exception as e:
            print(e)
            return False

    def receiveCommand(self, timeout):
        try:
            #self.openPort()
            if self.comSerial.is_open:
                start = time.time()
                while True:
                    length = 1024 * 10
                    revBytes = self.comSerial.read(length)
                    if revBytes!= b'':
                        #print(revBytes)
                        crcH, crcL = self.calcCRC16(revBytes[1:], len(revBytes)-3)
                        if crcH == revBytes[-2]  and crcL == revBytes[-1]:
                            if revBytes[2] == 0x00 and revBytes[3] == 0x01:
                                if revBytes[4] == COM_ACK_SUCCESS:
                                    return (True, revBytes)
                                else:
                                    return  (False, None)
                            else:
                                return (True, revBytes)
                        return  (False, None)
                    end = time.time()
                    if (end - start) > timeout :
                        #self.closePort()
                        return  (False, None)
                    time.sleep(0.3)
            else:
                return  (False, None)
        except Exception as e:
            #self.closePort()
            print("uart data receive  error")
            return (False, None)

