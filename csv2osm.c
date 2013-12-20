#include <stdio.h>  
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <locale.h>
#include <stdlib.h>

#ifndef false
#define false 0
#endif
#ifndef true
#define true 1
#endif

enum{
	TYPE_NOTDEFINED,
	TYPE_POWER_STATION, // подстанция
	TYPE_POWER_SUB_STATION, // трансформаторная будка (ТП-шка)
	TYPE_POWER_LINE, // линия эл. передачи
	TYPE_POWER_LINE_04 // низковольтная линия эл. передачи
};
enum{
	POI_INDEX_NUM,
	POI_INDEX_SYMBOL
};
#define POI_TYPE int
#define INDEX_TYPE int


#ifndef bool
#define bool int
#endif

struct nameList
{
	wchar_t *name;
	struct nameList *next;
};

struct pointList
{
	POI_TYPE poi_type;
	struct nameList *line_name;
	int voltage;
	// имя poi
	wchar_t *poi_name;
	/* разбиваем имя poi на две части вида: prefix+index, 
	Например, для poi с именем 3/2/11:
	prefix=3/2/
	индекс=11
	Или для poi с именем 2а:
	prefix=2
	индекс=а
	это нужно для ответвлений линий с именами опор вида: 2а, 2б,2в и т.п.
*/
	// часть имени poi 
	wchar_t *poi_name_prefix;
	wchar_t *poi_name_index;
	int poi_name_prefix_size;
	int poi_name_index_size;
	INDEX_TYPE poi_index_type;
	long double lat;
	long double lon;
	float ele;
	int id;
	bool link;
	bool first_in_line;
	bool last_in_line;
	struct pointList *next_element;
	struct pointList *next_list;
};

static void print_element_names(xmlNode * a_node);  
int parsing_GeoObject(xmlNode * a_node);
static void parsing_Polygon(xmlNode * a_node);
int parsing_posList(xmlChar * text);
void free_posList(struct pointList *List);
void free_nameList(struct nameList *list);
wchar_t *get_next_value(wchar_t *src_str, wchar_t **dsp_ptr, int *len_dst, wchar_t delimiter);
int add_element(POI_TYPE type,wchar_t *line_name,int voltage,wchar_t *poi_name,long double lat,long double lon,float ele);
struct nameList *find_line_name(wchar_t *line_name);
struct pointList *find_nearest(struct pointList *list, struct pointList *element);
struct pointList *get_nearest(struct pointList *list, struct pointList *element);
struct pointList *find_lightest_number_name(struct pointList *list);
struct pointList *find_lightest_symbol_index_name_with_prefix(struct pointList *list, wchar_t *prefix_to_find);
struct pointList *find_lightest_number_name_with_prefix(struct pointList *list, wchar_t *prefix_to_find);
struct pointList *get_first_point(struct pointList *list, wchar_t *prefix, int prefix_size);
struct pointList *get_way_point(struct pointList *list, wchar_t *prefix, int prefix_size);
struct pointList* line_have_two_or_more_points(struct pointList *list, wchar_t *prefix, int prefix_size);
wchar_t* skip_zero(wchar_t*str);
int wc_cmp(wchar_t *s1, wchar_t *s2);
int process_command_options(int argc, char **argv);

// Список имён линий (станций):
struct nameList *lineNameList=0;
struct nameList **lineNameList_last=&lineNameList;

// Двумерный связный список линий (подстанций) (по горизонтали - элементы линии, по вертикали - линии)
struct pointList *posList=0;
int posList_num=0;
struct pointList **posList_last=&posList;
struct pointList *posList_cur=0;
int cur_global_id=-1;
int voltage=-1;
int prefix_size_max=0;

// Дополнительные теги по-умолчанию:
wchar_t def_operator[]=L"ОАО ДРСК";
wchar_t def_source[]=L"survey";
wchar_t def_source_note[]=L"импортировано с помощью gpx2ocs->ocs2osm на основании gpx-точек обходов линий"; 


char *in_file_name=NULL;
char *out_file_name=NULL;
FILE *in_file=NULL;
FILE *out_file=NULL;

int main(int argc, char **argv)
{  
	wchar_t buf[BUFSIZ];
    wchar_t *pch;
    size_t count;
	wchar_t *cur_col=0;
	int len_dst;
	wchar_t *dst=0;
	wchar_t *tmp=0;
	POI_TYPE type=TYPE_NOTDEFINED;
	wchar_t *line_name=0,*poi_name=0;
	long double lat,lon;
	float	ele;
	int error=0;
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC,"C");
	if(process_command_options(argc,argv)!=0)
	{
		fprintf(stderr, "\nUse -h for help.\nexit!\n");
		return -1;
	}
	if(in_file_name==NULL)
	{
		in_file=stdin;
	}
	else
	{
		in_file=fopen(in_file_name,"r");
		if(in_file==NULL)
		{
			fprintf(stderr, "\nerror open file %s (%s)",in_file_name,strerror(errno));
			return -1;
		}
	}
	if(out_file_name==NULL)
	{
		out_file=stdout;
		if(out_file==NULL)
		{
			fprintf(stderr, "\nerror open file %s (%s)",out_file_name,strerror(errno));
			return -1;
		}
	}
	else
	{
		out_file=fopen(out_file_name,"w");
		if(out_file==NULL)
		{
			fprintf(stderr, "\nerror open file %s (%s)",out_file_name,strerror(errno));
			return -1;
		}
	}
	do{
		error=fgetws(buf, BUFSIZ, in_file);
#ifdef DEBUG
		//fprintf(stderr,"DEBUG (%s:%i): line_name=%s, poi_name=%s, lat=%Lf, lon=%Lf, ele=%f\n", __FILE__,__LINE__,line_name,poi_name,lat,lon,ele );
#endif
		if(wcslen(buf)==1)
		{
			// пустая строка
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: Пустая строка! Пропуск строки!\n",__FILE__,__LINE__);
#endif
			continue;
		}
		// type
		tmp=get_next_value(buf, &dst, &len_dst, L';');
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: Значение ячейки 1: %ls\n",__FILE__,__LINE__,dst);
#endif

		if(!wcscmp(dst,L"station"))
			type=TYPE_POWER_STATION;
		else if(!wcscmp(dst,L"line"))
			type=TYPE_POWER_LINE;
		else if(!wcscmp(dst,L"substation"))
			type=TYPE_POWER_SUB_STATION;
		else if(!wcscmp(dst,L"line04"))
			type=TYPE_POWER_LINE_04;
		else
			type=TYPE_POWER_LINE;

		free(dst);
		// Имя линии
		tmp=get_next_value(tmp, &line_name, &len_dst, L';');
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: Значение ячейки 2: %ls\n",__FILE__,__LINE__,line_name);
#endif
		// Предполагаем напряжение по имени:
		if(!wc_cmp(line_name,L"ВЛ 0,4"))
			voltage=400;
		else if(!wc_cmp(line_name,L"ВЛ 6"))
			voltage=6000;
		else if(!wc_cmp(line_name,L"ВЛ 10"))
			voltage=10000;
		else if(!wc_cmp(line_name,L"ВЛ 35"))
			voltage=35000;
		else if(!wc_cmp(line_name,L"ВЛ 110"))
			voltage=110000;
		else if(!wc_cmp(line_name,L"ВЛ 220"))
			voltage=220000;
		else if(!wc_cmp(line_name,L"ПС 35"))
			voltage=35000;
		else if(!wc_cmp(line_name,L"ПС 110"))
			voltage=110000;
		else if(!wc_cmp(line_name,L"ПС 220"))
			voltage=220000;
		else
			// не смог определить:
			voltage=-1;
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: Определили значение напряжения: %i\n",__FILE__,__LINE__,voltage);
#endif

		// Имя точки
		tmp=get_next_value(tmp, &poi_name, &len_dst, L';');
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: Значение ячейки 3: %ls\n",__FILE__,__LINE__,poi_name);
#endif

		// lat
		tmp=get_next_value(tmp, &dst, &len_dst, L';');
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: Значение ячейки 4: %ls\n",__FILE__,__LINE__,dst);
#endif
		swscanf(dst,L"%Lf",&lat);
		free(dst);

		// lon
		tmp=get_next_value(tmp, &dst, &len_dst, L';');
		swscanf(dst,L"%Lf",&lon);
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: Значение ячейки 5: %ls, полученное значение: %Lf\n",__FILE__,__LINE__,dst,lon);
#endif
		free(dst);

		// ele
		tmp=get_next_value(tmp, &dst, &len_dst, L';');
		swscanf(dst,L"%f",&ele);
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: Значение ячейки 6: %ls, полученное значение: %f\n",__FILE__,__LINE__,dst,ele);
#endif
		free(dst);

		if(add_element(type,line_name,voltage,poi_name,lat,lon,ele)==-1)
		{
			fwprintf(stderr,L"%s:%i: error add_element()\n",__FILE__,__LINE__);
			// очистка памяти:
			if(out_file_name!=NULL)
				fclose(out_file);
			free_posList(posList);
			free_nameList(lineNameList);
			return -1;
		}
		if(line_name)free(line_name);
		if(poi_name)free(poi_name);
	}while(error!=0);
	if(in_file_name!=NULL)
		fclose(in_file);
	// Выводим OSM:
	if(print_osm()==-1)
	{
		fwprintf(stderr,L"%s:%i: error print_osm()\n",__FILE__,__LINE__);
		// очистка памяти:
		if(out_file_name!=NULL)
			fclose(out_file);
		free_posList(posList);
		free_nameList(lineNameList);
		return -1;
	}
	if(out_file_name!=NULL)
		fclose(out_file);
	// очистка памяти:
	free_posList(posList);
	free_nameList(lineNameList);
	return 0;
}

// Получаем следующее значение:
wchar_t *get_next_value(wchar_t *src_str, wchar_t **dst_ptr, int *len_dst, wchar_t delimiter)
{
	wchar_t *pch=0;
	int begin_index=0;
	int count=0;

	// пропуск пробелов в начале слова:
    for(pch = src_str, count = 0; *pch == L' ' && count<wcslen(src_str)-1; pch++, count++);
	begin_index=count;

    for(pch = src_str, count = 0; *pch != delimiter && count<wcslen(src_str)-1; pch++, count++);
#ifdef DEBUG
	fwprintf(stderr,L"%s:%i: длинна слова: %i utf-символов (длинна переданной строки: %i)\n",__FILE__,__LINE__,count,wcslen(src_str)-1);
#endif
	*len_dst=(count+1-begin_index)*sizeof(wchar_t);
	*dst_ptr=(wchar_t*)malloc(*len_dst);
	if(!*dst_ptr)
	{
		fwprintf(stderr,L"%s:%i: ERROR malloc(%i)\n",__FILE__,__LINE__,*len_dst);
		return NULL;
	}
	wcpncpy(*dst_ptr,src_str+begin_index,count-begin_index);
	*(*dst_ptr+count-begin_index)=L'\0';
	return ++pch;
}

// Ищем строку в списке, если нет, то создаём новый элемент, копируем туда строку, возвращаем указатель на строку
struct nameList *find_line_name(wchar_t *line_name)
{
	lineNameList_last=&lineNameList;
	// поиск:
	while(*lineNameList_last)
	{
		if(!wcscmp((*lineNameList_last)->name,line_name))
		{
			return *lineNameList_last;
		}
		lineNameList_last=&((*lineNameList_last)->next);
	}
	// Добавляем новый элемент и возвращаем указатель на него:
	*lineNameList_last=malloc(sizeof(struct nameList));
	if(!*lineNameList_last)
	{
		fwprintf(stderr,L"%s:%i: ERROR malloc(%i)\n",__FILE__,__LINE__,sizeof(struct nameList));
		return NULL;
	}
	memset(*lineNameList_last,0,sizeof(struct nameList));
	(*lineNameList_last)->name=malloc((wcslen(line_name)+1)*sizeof(wchar_t));
	if(!(*lineNameList_last)->name)
	{
		fwprintf(stderr,L"%s:%i: ERROR malloc(%i)\n",__FILE__,__LINE__,(wcslen(line_name)+1)*sizeof(wchar_t));
		free(*lineNameList_last);
		*lineNameList_last=0;
		return NULL;
	}
	wcpcpy((*lineNameList_last)->name,line_name);
	return *lineNameList_last;
}

int add_element(POI_TYPE type, wchar_t *line_name,int voltage, wchar_t *poi_name,long double lat,long double lon,float ele)
{
	struct nameList *line_name_from_list=0;
	// Поиск подходящего списка:
	line_name_from_list=find_line_name(line_name);
	if(!line_name_from_list)
	{
		fwprintf(stderr,L"%s:%i: ERROR find_line_name()\n",__FILE__,__LINE__);
		return -1;
	}
	// Ищем цепочку линии с нужным именем (по вертикали):
	posList_last=&posList;
	while(*posList_last)
	{
		if((*posList_last)->line_name==line_name_from_list)
		{
			// Нашли линию
			// Проходим в конец списка элементов линии (по горизонтали):
			while(*posList_last)
				posList_last=&((*posList_last)->next_element);
			break;
		}
		posList_last=&((*posList_last)->next_list);
	}
	// Добавляем новый элемент
	// выделяем память под элемент списка:
	*posList_last=malloc(sizeof(struct pointList));
	if(!*posList_last)
	{
		fprintf(stderr,"%s:%i: Error aalocate %i bytes!\n",__FILE__,__LINE__,sizeof(struct pointList));
		return -1;
	}
	memset(*posList_last,0,sizeof(struct pointList));
	// Заполняем структуру:
	(*posList_last)->poi_name=malloc((wcslen(poi_name)+1)*sizeof(wchar_t));
	if(!(*posList_last)->poi_name)
	{
		fwprintf(stderr,L"%s:%i: ERROR malloc(%i)\n",__FILE__,__LINE__,(wcslen(poi_name)+1)*sizeof(wchar_t));
		free(*posList_last);
		*posList_last=0;
		return -1;
	}
	wcpcpy((*posList_last)->poi_name,poi_name);
	(*posList_last)->line_name=line_name_from_list;
	(*posList_last)->voltage=voltage;
	(*posList_last)->poi_type=type;
	(*posList_last)->lat=lat;
	(*posList_last)->lon=lon;
	(*posList_last)->ele=ele;
	(*posList_last)->id=cur_global_id;
	cur_global_id--;

	return 0;
}

void free_posList(struct pointList *list)
{
	struct pointList *tmp;
	struct pointList *element_list;
	struct pointList *tmp_line_list;
	while(list)
	{
		tmp_line_list=list;
		element_list=list;
		list=list->next_list;
		// Очистка элементов:
		while(element_list)
		{
			tmp=element_list;
			element_list=element_list->next_element;
			if(tmp->poi_name)free(tmp->poi_name);
			if(tmp->poi_name_prefix)free(tmp->poi_name_prefix);
			if(tmp->poi_name_index)free(tmp->poi_name_index);
			free(tmp);
		}
		posList=0;
		posList_num=0;
		posList_last=&posList;
		posList_cur=0;
	}
}
void free_nameList(struct nameList *list)
{
	struct nameList *tmp;
		// Очистка элементов:
		while(list)
		{
			tmp=list;
			list=list->next;
			free(tmp->name);
			free(tmp);
		}
		lineNameList=0;
		lineNameList_last=&lineNameList;
}
			
int print_osm(void)
{
		struct pointList *element=0, *tmp=0, *first_element=0;
		// вывод шапки OSM xml:
		fwprintf(out_file,L"<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6' upload='true' generator='JOSM'>\n");
		fwprintf(out_file,L"<bounds minlat='39.21' minlon='128.24' maxlat='53.02' maxlon='142.44' origin='OpenStreetMap server' />\n");

		// Печатаем все точки:
		posList_last=&posList;
		while(*posList_last)
		{
			element=*posList_last;
			while(element)
			{
				// Печатаем точки:
				fwprintf(out_file,L"\
  <node id='%i' action='modify' visible='true' lat='%.8Lf' lon='%.8Lf' >\n",element->id,element->lat,element->lon);
				if((*posList_last)->poi_type==TYPE_POWER_LINE)
				{
					// Опоры Высоковольтныч линий
					fwprintf(out_file,L"    <tag k='ref' v='%ls' />\n",element->poi_name);
					fwprintf(out_file,L"    <tag k='operator' v='%ls' />\n",def_operator);
					fwprintf(out_file,L"    <tag k='power' v='tower' />\n");
					fwprintf(out_file,L"    <tag k='note' v='%ls' />\n",element->line_name->name);
				}
				else if((*posList_last)->poi_type==TYPE_POWER_LINE_04)
				{
					// Столбы низковольтных линий
					fwprintf(out_file,L"    <tag k='ref' v='%ls' />\n",element->poi_name);
					fwprintf(out_file,L"    <tag k='operator' v='%ls' />\n",def_operator);
					fwprintf(out_file,L"    <tag k='power' v='pole' />\n");
					fwprintf(out_file,L"    <tag k='note' v='%ls' />\n",element->line_name->name);
				}
				else if((*posList_last)->poi_type==TYPE_POWER_SUB_STATION)
				{
					fwprintf(out_file,L"    <tag k='ref' v='%ls' />\n",element->poi_name);
					fwprintf(out_file,L"    <tag k='operator' v='%ls' />\n",def_operator);
					fwprintf(out_file,L"    <tag k='power' v='sub_station' />\n");
					// На ТП есть только ref, а "общее имя им не нужно"
					//fwprintf(out_file,L"    <tag k='name' v='%ls' />\n",element->line_name->name);
				}
				else if((*posList_last)->poi_type==TYPE_POWER_STATION)
				{
					// Точкам полигона подстанции не печатаем теги подстанции, только тег 'note':
					//fwprintf(out_file,L"    <tag k='ref' v='%ls' />\n",element->poi_name);
					//fwprintf(out_file,L"    <tag k='operator' v='%ls' />\n",def_operator);
					//fwprintf(out_file,L"    <tag k='power' v='station' />\n");
					fwprintf(out_file,L"    <tag k='note' v='%ls' />\n",element->line_name->name);
				}
				// печатаем остальные теги точек:
				fwprintf(out_file,L"    <tag k='ele' v='%f' />\n",element->ele);
				fwprintf(out_file,L"    <tag k='source' v='%ls' />\n",def_source);
				fwprintf(out_file,L"    <tag k='source:note' v='%ls' />\n",def_source_note);

				fwprintf(out_file,L"    </node>\n");


				element=element->next_element;
			}
			posList_last=&((*posList_last)->next_list);
		}
		// печатаем пути для подстанций
		if(posList->poi_type==TYPE_POWER_STATION)
		{
				posList_cur=posList;
				// пока есть неиспользуемые точки:
				while(posList_cur)
				{
					//element=get_nearest(posList_cur,posList_cur);
					// начинаем с точки, имеющий наименьший номер в наименовании:
					element=find_lightest_number_name(posList_cur);
					while(element)
					{
							// проверяем, будут ли ещё точки в этой линии - если будут - рисуем линию:
							if(find_nearest(posList_cur,element))
							{
									fwprintf(out_file,L"  <way id='%i' action='modify' visible='true'>\n",cur_global_id);
									cur_global_id--;
									first_element=element;
									while(element)
									{
										// Добавляем точки в линию:
										fwprintf(out_file,L"        <nd ref='%i' />\n",element->id);
										element=get_nearest(posList_cur,element);
									}
									// если полигон, то нужно "замыкать":
									if(posList_cur->poi_type==TYPE_POWER_STATION)
									{
										fwprintf(out_file,L"        <nd ref='%i' />\n",first_element->id);
									}
									// печатаем дополнительные теги:
									if(print_way_tags(posList_cur)==-1)
									{
											fwprintf(stderr,L"%s:%i: error print_way_tags()\n",__FILE__,__LINE__);
											return -1;
									}

									fwprintf(out_file,L"  </way>\n");
							}
							else
							{
								// отмечаем, что этот элемент может обрабатывался:
								element->first_in_line=1;
							}
							// Проверяем, есть ли ещё неиспользованные точки в этой линии:
							element=posList_cur;
							while(element)
							{
								if(!element->link&&!element->first_in_line&&!element->last_in_line)
								{
									// найден неиспользованный элемент
#ifdef DEBUG
									fwprintf(stderr,L"%s:%i: найден неиспользуемый элемент.\n\
Линия: %Ls\n\
poi_name: %Ls\n\
id: %i \n",__FILE__,__LINE__,element->line_name->name,element->poi_name,element->id);
#endif
									break;
								}
								element=element->next_element;
							}
					}

					posList_cur=posList_cur->next_list;
				}
		}

		// печатаем пути для линий
		if(posList->poi_type==TYPE_POWER_LINE||posList->poi_type==TYPE_POWER_LINE_04)
		{
#ifdef DEBUG
				fwprintf(stderr,L"%s:%i: Начинаем рисование линий\n",__FILE__,__LINE__);
#endif
				// последовательно печатаем все линии:
				posList_cur=posList;
				while(posList_cur)
				{
					// обрабатываем все линии 
					if(print_line(posList_cur)==-1)
						return -1;
					posList_cur=posList_cur->next_list;
				}
		}

		// завершаем OSM xml:
		fwprintf(out_file,L"</osm>\n");
		fflush(out_file);


		return (0);  
}
int print_line(struct pointList *posList)
{
	int prefix_size=0;
	/* Предвариетльная обработка точек:
	 * формирование префиксов 
	 * формирование индексов
	 */
	if(prepare_line_list(posList)==-1)
	{
		fwprintf(stderr,L"%s:%i: error prepare_line_list()\n",__FILE__,__LINE__);
		return -1;
	}
	// в цикле от длинны самого короткого префикса до самого длинного префикса
	for(prefix_size=0;prefix_size<=prefix_size_max;prefix_size++)
	{
		/* рисуем линии сначала с меньшими префиксами, затем
		с большими префиксами
		*/
		if(print_line_by_prefix(posList,prefix_size)==-1)
		{
			fwprintf(stderr,L"%s:%i: error print_line_by_prefix()\n",__FILE__,__LINE__);
			return -1;
		}
	}
	return 0;
}

int print_line_by_prefix(struct pointList *list, int prefix_size)
{
		struct pointList *first_element=0, *tmp=0;
		wchar_t *prefix=0;
		INDEX_TYPE index_type=0;
		first_element=list;
		// пока есть неиспользуемые точки:
		while(list)
		{
			// ищем точку с заданной длинной префикса:
			if(!list->link && list->poi_name_prefix_size==prefix_size)
			{
				prefix=list->poi_name_prefix;
				index_type=list->poi_index_type;
#ifdef DEBUG
				fwprintf(stderr,L"%s:%i: Обрабатываем линию с префиксом: %ls, тип: %i\n",__FILE__,__LINE__,prefix, index_type);
#endif
				/* проверяем, если точек в линии больше 1, то рисуем линию: */
				if(!line_have_two_or_more_points(list,prefix,prefix_size))
				{
#ifdef DEBUG
					fwprintf(stderr,L"%s:%i: пропускаем линию с менее чем 2-мя точками (poi_name=%ls, prefix=%ls)\n",__FILE__,__LINE__,list->poi_name,prefix);
#endif
					list=list->next_element;
					continue;
				}
				// рисуем линию с этим префиксом:
				fwprintf(out_file,L"  <way id='%i' action='modify' visible='true'>\n",cur_global_id);
				cur_global_id--;
				
				// ищем начальную точку:
				// только для линий с префиксом:
				if(prefix_size)
				{
					tmp=get_first_point(first_element, prefix, prefix_size);
					if((int)tmp==-1)
					{
						fwprintf(stderr,L"%s:%i: error get_first_point()\n",__FILE__,__LINE__);
						return -1;
					}else if(tmp)
					{
						fwprintf(out_file,L"        <nd ref='%i' />\n",tmp->id);
					}
				}
				/* печатаем остальные опоры в линии */
				do
				{
					/* в цикле печатаем конкретную линию, пока не закончатся 
					оборы с таким префиксом 
					*/
					tmp=get_way_point(list, prefix, prefix_size);
					if((int)tmp==-1)
					{
						fwprintf(stderr,L"%s:%i: error get_way_point()\n",__FILE__,__LINE__);
						return -1;
					}
					else if(tmp)
					{
						// если нашли, то добавляем в линию
						fwprintf(out_file,L"        <nd ref='%i' />\n",tmp->id);
						tmp->link=1;
					}
				}while(tmp);
				/* печатаем теги */
				if(print_way_tags(list)==-1)
				{
					fwprintf(stderr,L"%s:%i: error print_way_tags()\n",__FILE__,__LINE__);
					return -1;
				}
				fwprintf(out_file,L"  </way>\n");
			}
			list=list->next_element;
		}
	return 0;
}
int print_way_tags(struct pointList *list)
{
	// печатаем остальные теги точек:
	if(list->poi_type==TYPE_POWER_STATION)
	{
		fwprintf(out_file,L"    <tag k='power' v='station' />\n");
		if(list->voltage!=-1)
			fwprintf(out_file,L"    <tag k='voltage' v='%i' />\n",list->voltage);
	}
	else if(list->poi_type==TYPE_POWER_LINE)
	{
		fwprintf(out_file,L"    <tag k='power' v='line' />\n");
		if(list->voltage!=-1)
			fwprintf(out_file,L"    <tag k='voltage' v='%i' />\n",list->voltage);
	}
	else if(list->poi_type==TYPE_POWER_LINE_04)
	{
		fwprintf(out_file,L"    <tag k='power' v='minor_line' />\n");
		fwprintf(out_file,L"    <tag k='voltage' v='400' />\n");
	}
	else
	{
		// по умолчанию - линия:
		fwprintf(out_file,L"    <tag k='power' v='line' />\n");
	}
	fwprintf(out_file,L"    <tag k='name' v='%ls' />\n",list->line_name->name);
	fwprintf(out_file,L"    <tag k='operator' v='%ls' />\n",def_operator);
	fwprintf(out_file,L"    <tag k='source' v='%ls' />\n",def_source);
	fwprintf(out_file,L"    <tag k='source:note' v='%ls' />\n",def_source_note);
	return 0;
}

struct pointList *get_way_point(struct pointList *list, wchar_t *prefix, int prefix_size)
{

		struct pointList *tmp=0;
		/* В цикле просматриваем все необработанные опоры
		*/
		while(list)
		{
			/* ищем опоры с префиксом, равным заданному
			*/
			if(!list->link && list->poi_name_prefix_size==prefix_size)
			{
				if(list->poi_index_type==POI_INDEX_NUM)
				{
					return find_lightest_number_name_with_prefix(list, prefix);
				}
				else if(list->poi_index_type==POI_INDEX_SYMBOL)
				{
					return find_lightest_symbol_index_name_with_prefix(list, prefix);
				}
			}
			list=list->next_element;
		}
		return 0;
}

/* Проверка, содержит ли линия с таким префиксом более чем одну точку.
 * Если не содержит, то линию рисовать нельзя - JOSM сообщит об ошибке - 
 * не может быть линии из одной точки
 */
struct pointList* line_have_two_or_more_points(struct pointList *list, wchar_t *prefix, int prefix_size)
{
	int num_points=0;
	struct pointList *point1=0, *point2=0;

	/* пробуем найти точку, к которой присоединяется данная линия (с указанным
	 * префиксом)
	 */
	point1=get_first_point(list,prefix,prefix_size);
	if(point1==-1)
	{
		fwprintf(stderr,L"%s:%i: ERROR get_first_point()\n",__FILE__,__LINE__);
		return -1;
	}else if(point1)num_points++;

	/* пробуем найти первую точку с указанным префиксом 
	 */
	point1=get_way_point(list, prefix, prefix_size);
	if(point1==-1)
	{
		fwprintf(stderr,L"%s:%i: error get_way_point()\n",__FILE__,__LINE__);
		return -1;
	}
	else if(point1)
	{
		num_points++;
		/* если нашли достаточно точек
		 */
		if(num_points>1)return 1;
		else
		{
			/* пробуем искать вторую точку, для чего временно исключаем первую из списка:
			 */
			point1->link=1;
		}
	}
	else
	{
		/* не нашли точки с этим префиксом */
		return 0;
	}
	/* пробуем найти вторую точку с тем же префиксом */
	point2=get_way_point(list, prefix, prefix_size);
	if(point2==-1)
	{
		fwprintf(stderr,L"%s:%i: error get_way_point()\n",__FILE__,__LINE__);
		return -1;
	}
	else if(point2)
	{
		num_points++;
	}
	/* освобождаем первую точку линии
	 */
	if(point1)point1->link=0;

	if(num_points>1)return 1;
	else return 0;
}

struct pointList *get_first_point(struct pointList *list, wchar_t *prefix, int prefix_size)
{
	wchar_t *tmp_name=0;
	/* ищем начальную точку
	 начальная точка - это опора с именем, равным префиксу.
	*/
	tmp_name=(wchar_t*)malloc(
		(prefix_size+1)*sizeof(wchar_t)
	);
	if(!tmp_name)
	{
		fwprintf(stderr,L"%s:%i: ERROR malloc(%i)\n",__FILE__,__LINE__,	(prefix_size)*sizeof(wchar_t) );
		return -1;
	}
	wcpncpy(tmp_name,prefix,prefix_size);
	if(	*(prefix+prefix_size-1)==L'/'||
		*(prefix+prefix_size-1)==L'\\'
	)
	{
		/* Если у префикса последний символ слэш,
		то укорачиваем искомое имя на один символ
		*/
		*(tmp_name+prefix_size-1)=L'\0';
	}
	else
		*(tmp_name+prefix_size)=L'\0';
	/* ищем во всех опорах, включая обработанные */
	while(list)
	{
		if(!wcscmp(list->poi_name,tmp_name))
		{
			// если нашли, то добавляем в начало линии
			free(tmp_name);
			return list;
		}
		list=list->next_element;
	}
	free(tmp_name);
	return 0;
}

int prepare_line_list(struct pointList *list)
{
	int index=0;
	int len=0;
	while(list)
	{
		// анализируем последний символ имени poi:
		len=wcslen(list->poi_name);
		if(len<1)
		{
			fwprintf(stderr,L"%s:%i: empty poi_name!\n",__FILE__,__LINE__);
			return -1;
		}
		if(iswdigit(*(list->poi_name+len-1)))
		{
			// число
			list->poi_index_type=POI_INDEX_NUM;
			// получаем префикс:
			// ищем первый нецифровой символ с конца:
			for(index=len-1;index>=0;index--)
			{
				if(!iswdigit(*(list->poi_name+index)))
				{
					// не число
					break;
				}
			}
		}
		else
		{
			// не число
			list->poi_index_type=POI_INDEX_SYMBOL;
			// получаем префикс:
			// ищем первый нецифровой символ с конца:
			for(index=len-1;index>=0;index--)
			{
				if(iswdigit(*(list->poi_name+index)))
				{
					// число
					break;
				}
			}
		}
		if(fill_prefix_and_index(list,len,index)==-1)
		{
			fwprintf(stderr,L"%s:%i: ERROR fill_prefix_and_index()\n",__FILE__,__LINE__);
			return -1;
		}
		list=list->next_element;
	}
	return 0;
}
int fill_prefix_and_index(struct pointList *list, int len, int index)
{
	if(index>=0)
	{
		// нашли префикс, добавляем в элемент:
		list->poi_name_prefix=(wchar_t*)malloc(
			(index+2)*sizeof(wchar_t)
			);
		if(!list->poi_name_prefix)
		{
			fwprintf(stderr,L"%s:%i: ERROR malloc(%i)\n",__FILE__,__LINE__,	(index+2)*sizeof(wchar_t) );
			return -1;
		}
		list->poi_name_prefix_size=index+1;
		wcpncpy(list->poi_name_prefix,list->poi_name,index+1);
		*(list->poi_name_prefix+index+1)=L'\0';
		if(prefix_size_max<index+1)prefix_size_max=index+1;
		// добавляем индекс:
		list->poi_name_index=(wchar_t*)malloc(
			(len-index+1)*sizeof(wchar_t)
			);
		if(!list->poi_name_index)
		{
			fwprintf(stderr,L"%s:%i: ERROR malloc(%i)\n",__FILE__,__LINE__,	(len-index+1)*sizeof(wchar_t) );
			return -1;
		}
		list->poi_name_index_size=len-index-1;
		wcpncpy(list->poi_name_index,list->poi_name+index+1,len-index-1);
		*(list->poi_name_index+(len-index-1))=L'\0';
	}
	else
	{
		// если префикса не нашли, значит в индекс добавляем полное имя
		list->poi_name_index=(wchar_t*)malloc(
			(len+1)*sizeof(wchar_t)
			);
		if(!list->poi_name_index)
		{
			fwprintf(stderr,L"%s:%i: ERROR malloc(%i)\n",__FILE__,__LINE__,	(len+1)*sizeof(wchar_t) );
			return -1;
		}
		list->poi_name_index_size=len;
		wcpncpy(list->poi_name_index,list->poi_name,len);
		*(list->poi_name_index+len)=L'\0';
	}
	return 0;
}

struct pointList *get_nearest(struct pointList *list, struct pointList *element)
{
	unsigned int name=-1,cur_name=-1,from_name=-1;
	struct pointList *cur_item=0;
	long double kg_cur=0,kg_new=0;

	// имя точки, от которой ведём:
	swscanf(skip_zero(element->poi_name),L"%i",&from_name);
	element->first_in_line=1;

	cur_item=find_nearest(list, element);

	if(cur_item)
	{
		cur_item->link=1;
	}
	else
	{
		// помечаем предыдущую точку в линии, как последнюю в линии:
		if(!element->first_in_line)
		{
			element->link=0;
		}
		else
		{
			element->link=1;
		}
		element->last_in_line=1;
	}
	return cur_item;
}

struct pointList *find_nearest(struct pointList *list, struct pointList *element)
{
	unsigned int name=-1,cur_name=-1,from_name=-1;
	struct pointList *cur_item=0;
	long double kg_cur=0,kg_new=0;

	// имя точки, от которой ведём:
	swscanf(skip_zero(element->poi_name),L"%i",&from_name);

	// Подыскиваем нужную точку:
	while(list)
	{
		// Если уже подсоединяли эту точку в линию - пропускаем:
		if(!list->link)
		{
				swscanf(skip_zero(list->poi_name),L"%i",&name);
#ifdef DEBUG
				fwprintf(stderr,L"%s:%i: Значение имени точки: %ls, преобразовали в цифровое: %i\n",__FILE__,__LINE__,list->poi_name,name);
				fwprintf(stderr,L"%s:%i: cur_name: %i\n",__FILE__,__LINE__,cur_name);
#endif
				// самый маленький (в числовом варианте), но больший, чем точка, от которой ведём
				if(name<cur_name&&name>from_name)
				{
					cur_name=name;
					cur_item=list;
					kg_cur=(element->lat - list->lat)*(element->lat - list->lat)+(element->lon - list->lon)*(element->lon - list->lon);
				}
				if(name==cur_name)
				{
					// нашли дубль имени
					// решаем какой использовать - вычисляем какая линия находится ближе - ту и используем:
					kg_new=(element->lat - list->lat)*(element->lat - list->lat)+(element->lon - list->lon)*(element->lon - list->lon);
					if(kg_new<kg_cur)
					{
						// квадрат гепотинузы до новой точки меньше, значит берём её:
						cur_name=name;
						cur_item=list;
						kg_cur=kg_new;

					}
				}
		}
		list=list->next_element;
	}
	return cur_item;
}

/*
  Ищем в переданном списке опор опоры с заданным префиксом и минимальным буквенным индексом
  */
struct pointList *find_lightest_symbol_index_name_with_prefix(struct pointList *list, wchar_t *prefix_to_find)
{
	wchar_t *min_index=0;
	int min_index_size=0;
	int prefix_to_find_len=0;
	int equal_prefix=0;
	struct pointList *cur_item=0;
	int index=0;

	if(prefix_to_find)
			prefix_to_find_len=wcslen(prefix_to_find);

	// Подыскиваем нужную точку:
	while(list)
	{
		equal_prefix=0;
		// проверяем, что опора не используется, тип индекса - цифровой, размеры префиксов равны
		if(!list->link && list->poi_index_type==POI_INDEX_SYMBOL && prefix_to_find_len == list->poi_name_prefix_size)
		{
			if(list->poi_name_prefix!=0 && prefix_to_find!=0)
			{
				// не пустые префиксы
				if(!wcscmp(list->poi_name_prefix,prefix_to_find))
				{
					equal_prefix=1;
				}
			}
			else
			{
				// оба пустые префикса (размеры равны нулю)
				equal_prefix=1;
			}
			if(equal_prefix)
			{
				if(!min_index)
				{
					// берём этот
					min_index_size=list->poi_name_index_size;
					min_index=(wchar_t*)malloc(
							(min_index_size+1)*sizeof(wchar_t)
							);
					if(!min_index)
					{
						fwprintf(stderr,L"%s:%i: ERROR malloc(%i)\n",__FILE__,__LINE__,	(min_index_size+1)*sizeof(wchar_t) );
						return -1;
					}
					wcpncpy(min_index,list->poi_name_index,min_index_size);
					*(min_index+min_index_size)=L'\0';
					cur_item=list;
				}
				else
				{
					if(min_index_size>list->poi_name_index_size)
					{
						/* новое значение меньше по количеству знаков в индексе
						перезаписываем поверх, т.к. буфера хватит
						*/
						wcpncpy(min_index,list->poi_name_index,list->poi_name_index_size);
						*(min_index+list->poi_name_index_size)=L'\0';
						cur_item=list;
					}
					else if(min_index_size==list->poi_name_index_size)
					{
						for(index=0;index<min_index_size;index++)
						{
							if(	(unsigned int)*(min_index+index) >
								(unsigned int)*(list->poi_name_index+index)
							)
							{
								/* если числовой код символа в min_index больше, чем символ в list->poi_name_index в
								той же позиции, то переопределяем min_index
								*/
								/* новое значение такое же по количеству знаков в индексе
								перезаписываем поверх, т.к. буфера хватит
								*/
								wcpncpy(min_index,list->poi_name_index,list->poi_name_index_size);
								*(min_index+list->poi_name_index_size)=L'\0';
								cur_item=list;
							}
						}
					}
					/* если min_index_size меньше list->poi_name_index_size, то пропускаем этот элемент,
					т.к. значение индекса будет по определению больше
					*/
				}
			}
		}
		list=list->next_element;
	}
	if(min_index)
	{
		free(min_index);
	}
	if(cur_item)
	{
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: %s: Нашёл индекс %ls в точке с префиксом %ls\n",__FILE__,__LINE__,__FUNCTION__,cur_item->poi_name_index, prefix_to_find);
#endif
		return cur_item;
	}
	else
	{
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: %s: НЕ нашёл точек с префиксом %ls\n",__FILE__,__LINE__,__FUNCTION__, prefix_to_find);
#endif
		return 0;
	}
}

/*
  Ищем в переданном списке опор опоры с заданным префиксом и минимальным числовым индексом
  */
struct pointList *find_lightest_number_name_with_prefix(struct pointList *list, wchar_t *prefix_to_find)
{
	int prefix_to_find_len=0;
	int cur_index=-1;
	unsigned int min_index=-1;
	int equal_prefix=0;
	struct pointList *cur_item=0;

	if(prefix_to_find)
			prefix_to_find_len=wcslen(prefix_to_find);

	// Подыскиваем нужную точку:
	while(list)
	{
		equal_prefix=0;
		// проверяем, что опора не используется, тип индекса - цифровой, размеры префиксов равны
		if(!list->link && list->poi_index_type==POI_INDEX_NUM && prefix_to_find_len == list->poi_name_prefix_size)
		{
			if(list->poi_name_prefix!=0 && prefix_to_find!=0)
			{
				// не пустые префиксы
				if(!wcscmp(list->poi_name_prefix,prefix_to_find))
				{
					equal_prefix=1;
				}
			}
			else
			{
				// оба пустые префикса (размеры равны нулю)
				equal_prefix=1;
			}
			if(equal_prefix)
			{
				swscanf(skip_zero(list->poi_name_index),L"%i",&cur_index);
				// самый маленький (в числовом варианте), но больший, чем точка, от которой ведём
				if(cur_index<min_index)
				{
					min_index=cur_index;
					cur_item=list;
				}
			}

		}
		list=list->next_element;
	}
	if(cur_item)
	{
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: %s: Нашёл индекс %i в точке с префиксом %ls\n",__FILE__,__LINE__,__FUNCTION__,min_index, prefix_to_find);
#endif
		return cur_item;
	}
	else
	{
#ifdef DEBUG
		fwprintf(stderr,L"%s:%i: %s: НЕ нашёл точек с префиксом %ls\n",__FILE__,__LINE__,__FUNCTION__, prefix_to_find);
#endif
		return 0;
	}
}

struct pointList *find_lightest_number_name(struct pointList *list)
{
	unsigned int name=-1,cur_name=-1,from_name=-1;
	struct pointList *cur_item=list;
	long double kg_cur=0,kg_new=0;

	// имя точки, от которой ведём:
	swscanf(skip_zero(list->poi_name),L"%i",&name);

	// Подыскиваем нужную точку:
	while(list)
	{
		swscanf(skip_zero(list->poi_name),L"%i",&cur_name);
		// самый маленький (в числовом варианте), но больший, чем точка, от которой ведём
		if(cur_name<name)
		{
			name=cur_name;
			cur_item=list;
		}
		list=list->next_element;
	}
#ifdef DEBUG
	fwprintf(stderr,L"%s:%i: %s: Нашёл минимальное наименование точки в линии: %ls, преобразовали в цифровое: %i\n",__FILE__,__LINE__,__FUNCTION__,cur_item->poi_name,name);
#endif
	return cur_item;
}

wchar_t* skip_zero(wchar_t*str)
{
	int index;
	wchar_t *cur_str=str;
	int len=wcslen(str);
	for(index=0;index<len;index++)
	{
		if(*(str+index)==L'0'&&index+1<len)
		{
			cur_str=str+index+1;
		}
		else
			break;
	}
	return cur_str;
}

/* Поиск вхождения в строку
   Возвращает 0, если s2 было найдено в s1,
   при этом s1 может не быть равно s2
*/
int wc_cmp(wchar_t *s1, wchar_t *s2)
{
	int not_equal=1;
	int s1_index=0, s2_index=0;
	for(s1_index=0;s1_index<wcslen(s1);s1_index++)
	{
		if(*(s1+s1_index)==*s2)
		{
			not_equal=0;
			for(s2_index=0;s2_index<wcslen(s2);s2_index++)
			{
				if(s1_index+s2_index>wcslen(s1))
				{
					return 0;
				}
				if(*(s1+s1_index+s2_index)!=*(s2+s2_index))
				{
					not_equal=1;
					break;
				}
			}
			if(!not_equal)
			break;
		}
	}
	return not_equal;
}

int process_command_options(int argc, char **argv)
{
	int aflag = 0;
	int bflag = 0;
	char *cvalue = NULL;
	int index;
	int c;
	opterr = 0;
	while ((c = getopt (argc, argv, "hi:o:")) != -1)
	{
		switch (c)
		{
				case 'i':
						in_file_name=optarg;
						break;
				case 'o':
						out_file_name=optarg;
						break;
				case 'h':
						// help
						fprintf (stderr, "This is convertor from csv to osm format.\n\
Use: \
%s -i input.csv -o out.osm \n\
 \n\
options: \n\
	-i file - input file with csv \n\
	-o file - output file with osm, where programm save result \n\
	-h - this help\n\
\nIf input or output files not setted - programm used stdin or/and stdout", argv[0]);
						return 1;
				case '?':
						if (isprint (optopt))
								fprintf (stderr, "Unknown option: '-%c'.\n", optopt);
						else
								fprintf (stderr, "Unknown option character: '\\x%x'.\n", optopt);
						return 1;
				default:
						return 1;
		}
	}
	return 0;
}
