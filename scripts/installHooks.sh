#!/bin/bash


hookDir=$(git rev-parse --git-dir)/hooks

mkdir -p $hookDir
cp hooks/* $hookDir
