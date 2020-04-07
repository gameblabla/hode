#!/bin/sh
SOURCE="$1"
DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
if [ ! -d "$HOME/.hode" ]; then
mkdir $HOME/.hode
fi
./hode --datapath="$DIR" --savepath="$HOME/.hode"
