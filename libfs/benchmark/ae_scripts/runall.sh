#!/bin/bash

figure8/figure8.sh &> figure8.out
sleep 10
figure4/figure4.sh &> figure4.out
sleep 10
figure6/figure6.sh &> figure6.out
sleep 10
figure7/figure7.sh &> figure7.out
