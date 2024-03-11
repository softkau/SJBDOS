#!/bin/bash

APPS_DIR=$(dirname "$0")

rm ${APPS_DIR}/*/*.o
rm ${APPS_DIR}/*.o

echo cleaned all elfapps.