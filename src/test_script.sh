#!/bin/bash

#gnome-terminal --working-directory=$(echo $PWD) -e "for ((n=0; n < 100;n++)) do ./run_tests 0.0.0.0:50093 4 && sleep 0.3; done"
gnome-terminal -x bash -c "top -H -p $(pidof store) &; bash"
