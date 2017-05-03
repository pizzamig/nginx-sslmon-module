An ssl monitor for nginx
========================

SSLMON is a SSL MONitor module for nginx, providing statistical information about ssl.
It's designed to be used in a ssl terminator configuration, but it can be used also
wihtout upstream. In this case, the upstream time is always 0. 
Tested with nginx 1.10.3 and 1.12.0

How to build
------------
There are four patch files:

- patch-auto-modules
- patch-auto-options
- patch-src-event-ngx_event_openssl.c
- patch-src-http-ngx_http_request.c

to be applied to nginx sources; the patches are optional and provide handshake error statistics.
The patc

Then, you can configure nginx:

```bash
./configure --prefix=/opt/nginx --add-module=/path/to/sslmon --with-http_sslmon_module
```

The `--with-https_sslmon_module` option is provided by the patches.

Then, an easy

```bash
make && make install
```

will do the rest.

Preparation
-----------
The sslmon module write statistics in one file per worker:

- `/var/log/nginx/sslmon/sslmon-12345.log`
- `/var/log/nginx/sslmon/sslmon-12346.log`

where the number is the ProcessID of the worker.
The `/var/log/nginx/sslmon` directory has to exists and writeble from the user owner of the worker
(on FreeBSD systems is the `www` user, in other can be the `nginx` user)

Unfortunately, the path is currently hardcoded.

Configuration parameters
------------------------
The sslmon provides two parameters to customize the behavior:

```
sslmon_slow_request_time 100;
sslmon_update_period 15;
```

The sslmon module counts the number of slow request. A request is slow if it's response time
is greater then a specific threshold; this threshold can ne specified via the `sslmon_slow_request_time` parameter. The default value is 200 ms.

The sslmon rewrite the statistic file every 5 minutes. If you want slower or faster update, you can tune this period with the parameter `sslmon_update_period`, in seconds. The default value is 300 seconds (5 minutes)

Error Log
---------
The sslmon module can write to the error log.
Errors are logged at NGX_LOG_ERROR
Notice level provide only some status information.
Debug level can be really verbose.

Statistic provided
------------------
The collected statistics are:

request counter
slow request counter
ssl errors counter (handshake failures)
reused sessions counter
response time (avg)
upstream time (avg)
ssl time (avg) [response-upstream]

In the statistic log file you have also both configuration parameters and the timestamp expressed as epoch time.

An additional statistic information is the cipher type. It's a counters list of all matched ciphers used in the period. The list shows only the ciphers used in the last period, it's not a fixed list.

Collectd support
----------------
In the collectd directory, a shell script is provided to work with collectd.
The script reads all the statistic files and print them out (summed up) in a collectd compliant format.
The script can be used as exec plugin in collectd.
The script doesn't support the chiper counters


