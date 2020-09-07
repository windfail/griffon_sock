#private key
openssl genrsa -out key.pem 2048
#self-signed cert
openssl req -new   -x509  -key key.pem  -out yily.crt  -subj "/C=CN/ST=BeiJing/L=Beijing/O=Home/CN=Griffon"
