# plain
Plain is a small web server I am writing in C++ for fun and to explore more of the ways of efficient linux/posix IO.
It is meant to be a simple though efficient web server.

# performance
I did some very rudementary testing using *siege*. On small files (820 bytes in this case) plain is about 30% faster (in
transactions per second) than the default ubuntu instalation of nginx. On larger files (120 megabytes in this case) plain
is about 25% slower (in bytes per second) still. But I have some ideas to improve this still.

The concurrency score and response times on both large and small files are very good, so my IO scheduling seems fine.
