#./build/app/l2shaping -l 1-12 -n 2  -- -P -p 0x3 --config="(0,0,1),(1,0,2)" --dist-table="./dist/pareto.dist"
./build/app/l2shaping -l 1-12 -n 2  -- -P -p 0x3 --config="(0,0,1),(1,0,2)" --dist-table="./dist/chi_square6.dist"
#./build/app/l2shaping -l 1-11 -n 2  -- -P -p 0x15 --config="(2,0,1),(3,0,2)" --dist-table="./dist/normal.dist"

#r -l 1-11 -n 2  -- -P -p 0x3 --config="(0,0,1),(1,0,2)" --dist-table="./dist/normal.dist"

    
