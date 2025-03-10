all: seuphorica.js

seuphorica.js: seuphorica.cpp visutils.h
	em++ -O2 -std=c++11 seuphorica.cpp -o seuphorica.js \
          -s EXPORTED_FUNCTIONS="['_start', '_drop_hand_on', '_back_to_drawn', '_draw_to_hand', '_cast_spell', '_back_from_board', '_buy', '_back_to_shop', '_play', '_view_help', '_check_discard', '_back_to_game', '_update_dict', '_view_dictionary', '_sort_by', '_cheat', '_wild_become', '_view_game_log', '_view_new_game', '_set_language', '_restart', '_set_language_dic']" \
          -s EXTRA_EXPORTED_RUNTIME_METHODS='["FS","ccall"]' -sALLOW_MEMORY_GROWTH \
          -sFETCH

test: seuphorica.js
	cp index.html ~/public_html/seuphorica
	cp seuphorica.wasm ~/public_html/seuphorica
	cp seuphorica.js ~/public_html/seuphorica
	cp wordnik.txt ~/public_html/seuphorica
	cp slowa.txt ~/public_html/seuphorica
	cp german.txt ~/public_html/seuphorica
	cp french.txt ~/public_html/seuphorica
	cp fise-2.txt ~/public_html/seuphorica
	cp ptbr-v2.txt ~/public_html/seuphorica
	cp naughty-wordnik.txt ~/public_html/seuphorica
	cp naughty-slowa.txt ~/public_html/seuphorica
	cp naughty-german.txt ~/public_html/seuphorica
	cp naughty-french.txt ~/public_html/seuphorica
	cp naughty-fise-2.txt ~/public_html/seuphorica
	cp naughty-ptbr-v2.txt ~/public_html/seuphorica

