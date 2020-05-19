# if we have a hashtag at the start of the program then
# it is already an HTML colour code
/^#/ { print $0 }

# if its a color code then parse it and replace with
# an HTML color code
/^[^#]/ { printf("#%02x%02x%02x\n", $1, $2, $3) }
