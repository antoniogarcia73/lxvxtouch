
#define MAX_POINTS	10
#define MAX_NAME_LEN	64
#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)	((array[LONG(bit)] >> OFF(bit)) & 1)


#define ABSVAL_VAL        0
#define ABSVAL_MIN        1
#define ABSVAL_MAX        2
#define ABSVAL_FUZZ       3
#define ABSVAL_FLAT       4
#define ABSVAL_RESOLUTION 5

#define TOUCH_PORT	7777


typedef struct point {
  short id;
  unsigned short pressed;
  unsigned short x;
  unsigned short y;
} point;

typedef struct mtpoints {
  unsigned int   count;
  point          points[MAX_POINTS];
} mtpoints;

typedef struct tsinfo {
  char           name[MAX_NAME_LEN];
  unsigned short xmin;
  unsigned short xmax;
  unsigned short ymin;
  unsigned short ymax;
} tsinfo;

extern void ddsSendPoints( mtpoints *pointData, tsinfo *tsInfo );
