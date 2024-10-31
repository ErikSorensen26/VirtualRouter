#!/bin/bash

ip link add name test type dummy
ip link set test up

ip link add name test2 type dummy
ip link set test2 up