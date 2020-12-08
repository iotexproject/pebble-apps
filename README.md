# pebble-firmware-blockchain

pebble-firmware-blockchain read its order information from  iotex block chain node, and send IoT data to the subscriber's storage endpoint.


### Get RSA public key

Pebble device registration requires RSA public key, which can be obtained by following the steps below :

* Start the pebble device, it will send the RSA public key to the mqtt broker.

* You can get the public key at this address:   http://trypebble.io:8080/devices .

The picture below shows how to obtain the pebble RSA public key with a device id of 352656103381086 :

![image](https://github.com/iotexproject/pebble-firmware-blockchain/blob/main/doc/images/rsa_publick_key.png)


rsa_n and rsa_e are the required RSA public keys
