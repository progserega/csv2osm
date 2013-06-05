Convert csv to osm.

make
./csv2osm < input.csv > out.osm

input.csv have lines:

type;line name;poi number;lat;lon;ele
....
type;line name;poi number;lat;lon;ele

example:
line;ВЛ 35 Тимирязевка - Михайловка 1-34;10;44.88986103;133.95263620;23
line;ВЛ 35 Тимирязевка - Михайловка 1-34;11;44.89124587;133.95364432;21
line;ВЛ 35 Тимирязевка - Михайловка 1-34;12;44.89265494;133.95463088;24


Where type can be: line, station, substation.

If type is line, then all ponts add to line with name "line name".
If type is station, then all points is poins of polygon with name "line name".
If type is substation, then all points is poi.

"Poi number" - is simply number of point. It need for propertly create line from points. Line created
by numbers: 1->2->3....->110. But if in list have many identical numbers (example 2 and 2), then program
calculate which point is nearest and add poi's such:
1->2->2->3->4...->110

Becouse this parser used for parsiong of energy objects - tags, which added to osm-objects 
is power*. But it is simple to change to tags, which you need.
