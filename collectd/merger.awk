#!/usr/bin/env -S awk -f
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
	printf "epoch=%d\n",epoch
	printf "counter=%d\n",counter
	printf "slow_requests=%d\n", slow_requests
	printf "reused_sessions=%d\n", reused_sessions
	printf "avg_rt=%d\n", avg_rt
	printf "avg_ut=%d\n", avg_ut
	printf "avg_net_rt=%d\n", avg_net_rt
}
