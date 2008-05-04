set -e
set -x

for use_h in y n
do
    if [ y = $use_h ]
    then
        h_name=-header
        h_flag=-DHDRJUNK
    else
        h_name=
        h_flag=
    fi
    for use_f in y n
    do
        if [ y = $use_f ]
        then
            f_name=-before
            f_flag=-DPREJUNK
        else
            f_name=
            f_flag=
        fi
        for use_a in y n
        do
            if [ y = $use_a ]
            then
                a_name=-after
                a_flag=-DPOSTJUNK
            else
                a_name=
                a_flag=
            fi

            if [ x = x$h_name$f_name$a_name ]
            then
                suf=-std
            else
                suf=$h_name$f_name$a_name
            fi

            rm -f img.o
            cc -g -Wall -Werror -O $h_flag $f_flag $a_flag -DHAVE_CONFIG_H -c -o img.o img.c
            make
            cp upart upart$suf

            ./upart$suf -w regression-tests/junk$suf.img -c 20 -h 16 -s 63 svnd0
        done
    done
done


rm -f img.o
cc -g -Wall -Werror -O -DBIGMAJOR -DHAVE_CONFIG_H -c -o img.o img.c
make
cp upart upart-bigmajor

./upart-bigmajor -w regression-tests/bigmajor.img -c 20 -h 16 -s 63 svnd0


rm -f img.o
cc -g -Wall -Werror -O -DSMALLMAJOR -DHAVE_CONFIG_H -c -o img.o img.c
make
cp upart upart-smallmajor

./upart-smallmajor -w regression-tests/smallmajor.img -c 20 -h 16 -s 63 svnd0
