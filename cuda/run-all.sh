#!/bin/bash

cd cuda-data-stream/ ; ./run-filter-general ; cd ../
cd cuda-malloc/ ; ./run-filter-general ; cd ../
cd cuda-host-alloc/ ; ./run-filter-general ; cd ../
cd zero-copy/ ; ./run-filter-general ; cd ../