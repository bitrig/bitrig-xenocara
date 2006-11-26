#!/usr/bin/perl -T -w
# This is used  to add life to life
# This is a QUICK hack to convert life files to xlock's life format.
# Patterns MUST have <= 64 pts at start for life.c to use the data generated
# Below is an example of a life file without the first initial #'s
# Call the file glider.hlife and run it like xlockhexlife.pl < glider.hlife
#glider.hlife
##P -2 -2   Treated as a comment, program finds own center
#       . . O .
#      . . . O
#     O . . O
#    . . . O
#   . . . O

local($PTS, $X, $Y);

print "
Drop these points in life.c, within the 'patterns' array.
Note if the number of points > 64, one must increase points NUMPTS;
also to fit most screens and especially the iconified window,
one should have the size < 32x32.\n\n";
&search;
print "\npoints = $PTS; size = ${X}x$Y\n";

sub search {
    local ($row, $col, $firstrow, $firstcol);
    local ($i, $j, $found, $c, $tempx, $tempy);
    local (@array);

    $row = $col = 0;
    $firstrow = -1;
    $firstcol = 80;
    $PTS = $X = $Y = 0;
    while (<>) {
         if (!($_ =~ /^#/))
         {
            @chars = split(//);
            $col = 0;
            foreach $c (@chars) {
                if ($c =~ /[\*0O]/) {
                	$col++;
                    if ($firstrow < 0) {
                        $row = $firstrow = 1;
                    }
                    if ($col < $firstcol) {
                        $firstcol = $col;
                    }
                    if ($row > $Y) {
                        $Y = $row;
                    }
                    if ($col > $X) {
                        $X = $col;
                    }
                    $array{$col, $row} = 1;
                    $PTS++;
                } elsif ($c =~ /[\.]/) {
                	$col++;
            		}
            }
            $row++;
        }
    }
    $col = $X - $firstcol + 1;
    $row = $Y;
    print "    {\n        ";
    for ($j = 0; $j <= $Y; $j++) {
        $found = 0;
        for ($i = 0; $i <= $X; $i++) {
            if ($array{$i, $j}) {
                $found = 1;
                $tempx = $i - int(($col + 2) / 2) - $firstcol + 1;
                $tempy = $j - int(($row + 2) / 2);
                printf "$tempx, $tempy, ";
            }
        }
        if ($found) {
            print "\n        ";
        }
  }
  print "127\n    },\n";
  $X = $col;
}
