#!/bin/bash

run=1

if [ "$run" -eq 1 ]
then

if [ ! `pidof login-server_sql` ]
then
   screen -dmS login-server ./login-server_sql &
fi

if [ ! `pidof char-server_sql` ]
then
   screen -dmS char-server ./char-server_sql &
fi

if [ ! `pidof map-server_sql` ]
then
   screen -dmS map-server ./map-server_sql &
fi

sleep 10

./auto-restarter.sh &

fi