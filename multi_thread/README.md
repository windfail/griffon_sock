
# example
start server:
./netsock --server
start client 1, every 1second:
./netsock --client=1
start client 2, every 5second:
./netsock --client=5

client 1 output,  every 5s it will be blocked:
write at Mon Aug 10 22:19:13 2020 CST
read at Mon Aug 10 22:19:14 2020 CST
client 1 modified
write at Mon Aug 10 22:19:15 2020 CST
read at Mon Aug 10 22:19:16 2020 CST
client 1 modified
write at Mon Aug 10 22:19:17 2020 CST
read at Mon Aug 10 22:19:22 2020 CST
client 1 modified
write at Mon Aug 10 22:19:23 2020 CST
read at Mon Aug 10 22:19:24 2020 CST
client 1 modified

