#!/bin/sh
# Write games.txt for a PS64 games folder. A native PS5 app can't list folders
# under /data, so PS64 reads this list instead. Run it after adding games, then
# upload games.txt next to them:
#   tools/make_games_list.sh ~/c64games      # writes ~/c64games/games.txt
set -e
dir=${1:?usage: make_games_list.sh GAMES_DIR}
cd "$dir"
find . -type f ! -name 'games.txt' ! -name '.*' ! -path '*/.*' | sed 's#^\./##' | LC_ALL=C sort > games.txt.tmp
mv games.txt.tmp games.txt
echo "$(wc -l < games.txt | tr -d ' ') files listed in $dir/games.txt"
