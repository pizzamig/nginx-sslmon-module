#!/bin/sh
UPDATE_PERIOD=15
while true;
do
	start_epoch=$(date +%s)
	export TMPDIR=$PWD
	if [ -z "$(ls /var/log/nginx/sslmon/sslmon-* 2>/dev/null)" ]; then
		data_epoch=${start_epoch}
		requests=0
		slow_requests=0
		reused_sessions=0
		request_time=0
		upstream_time=0
		ssl_time=0
	else
		MERGEFILE=$(mktemp -t sslmon-XXX)
		./merger.awk /var/log/nginx/sslmon/* > ${MERGEFILE}
		data_epoch=$(awk -F= '/^epoc/ { print $2 }' ${MERGEFILE})
		if [ ${data_epoch} -eq 0 ]; then
			data_epoch=${start_epoch}
		fi
		requests=$(awk -F= '/^counter/ { print $2 }' ${MERGEFILE})
		slow_requests=$(awk -F= '/^slow_requests/ { print $2 }' ${MERGEFILE})
		reused_sessions=$(awk -F= '/^reused_sessions/ { print $2 }' ${MERGEFILE})
		request_time=$(awk -F= '/^avg_rt/ { print $2 }' ${MERGEFILE})
		upstream_time=$(awk -F= '/^avg_ut/ { print $2 }' ${MERGEFILE})
		ssl_time=$(awk -F= '/^avg_net_rt/ { print $2 }' ${MERGEFILE})
#		cat ${MERGEFILE}
		rm -rf ${MERGEFILE}
	fi
	#rrdtool updatev sslmon.rrd -s ${data_epoch}:${requests}:${slow_requests}:${reused_sessions}:${request_time}:${upstream_time}:${ssl_time}
	echo PUTVAL "" interval=$UPDATE_PERIOD ${data_epoch}:${requests}:${slow_requests}:${reused_sessions}:${request_time}:${upstream_time}:${ssl_time}
	now=$(date +%s)
	if [ $(( now - start_epoch )) -le $UPDATE_PERIOD ]; then
		sleep $(( $UPDATE_PERIOD + start_epoch - now ))
	fi
done
