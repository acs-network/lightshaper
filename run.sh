#./build/app/l3fwd -l 6-14 -n 2  -- -P -p 0x3 --config="(0,0,6),(0,1,7),(0,2,8),(0,3,9)"
#./build/app/l3fwd -l 6-14 -n 2  -- -P -p 0x3 --config="(1,0,6),(1,1,7),(1,2,8),(1,3,9),(0,0,10),(0,1,11)"
#./build/app/l3fwd -l 6-14 -n 2  -- -P -p 0x3 --config="(1,0,6),(1,1,7),(1,2,8),(1,3,9)"
./build/app/l2shaping -l 1-8 -n 2  -- -P -p 0x3 --config="(0,0,1),(1,0,2)"
