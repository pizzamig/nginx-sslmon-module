#!/bin/sh
UPDATE_PERIOD=15
IDENTIFIER="$(hostname)"/ssl/

merger()
{
	awk '
	BEGIN { epoch_counter=0;
		epoch=0;
		counter=0;
		slow_requests=0;
		reused_sessions=0;
		avg_rt=0.0;
		avg_ut=0.0;
		avg_net_rt=0.0;
		FS="=";
	}
	/^epoch/	{ if ( epoch < $2 )
				epoch = $2;
			  epoch_counter++;
			}
	/^counter/		{ counter = counter + $2 }
	/^slow_requests/	{ slow_requests = slow_requests + $2 }
	/^reused_sessions/	{ reused_sessions = reused_sessions + $2 }
	/^avg_rt/		{ avg_rt = avg_rt + $2 }
	/^avg_ut/		{ avg_ut = avg_ut + $2 }
	/^avg_net_rt/		{ avg_net_rt = avg_net_rt + $2 }
	END {
		if ( epoch_counter == 0 )
			epoch_counter = 1;
		avg_rt = avg_rt / epoch_counter;
		avg_ut = avg_ut / epoch_counter;
		avg_net_rt = avg_net_rt / epoch_counter;
		printf "epoch=%d\n",epoch;
		printf "counter=%d\n",counter;
		printf "slow_requests=%d\n", slow_requests;
		printf "reused_sessions=%d\n", reused_sessions;
		printf "avg_rt=%d\n", avg_rt;
		printf "avg_ut=%d\n", avg_ut;
		printf "avg_net_rt=%d\n", avg_net_rt;
	}' "$@"
}

while true;
do
	start_epoch=$(date +%s)
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
		merger /var/log/nginx/sslmon/* > ${MERGEFILE}
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
	echo PUTVAL "${IDENTIFIER}counter-requests" interval=$UPDATE_PERIOD ${data_epoch}:${requests}
	echo PUTVAL "${IDENTIFIER}counter-slow_requests" interval=$UPDATE_PERIOD ${data_epoch}:${slow_requests}
	echo PUTVAL "${IDENTIFIER}counter-reused_sessions" interval=$UPDATE_PERIOD ${data_epoch}:${reused_sessions}
	echo PUTVAL "${IDENTIFIER}total_time_in_ms-request_time" interval=$UPDATE_PERIOD ${data_epoch}:${request_time}
	echo PUTVAL "${IDENTIFIER}total_time_in_ms-upstream_time" interval=$UPDATE_PERIOD ${data_epoch}:${upstream_time}
	echo PUTVAL "${IDENTIFIER}total_time_in_ms-ssl_time" interval=$UPDATE_PERIOD ${data_epoch}:${ssl_time}
	now=$(date +%s)
	if [ $(( now - start_epoch )) -le $UPDATE_PERIOD ]; then
		sleep $(( $UPDATE_PERIOD + start_epoch - now ))
	fi
done
