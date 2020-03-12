#include <stdlib.h>
#include <string.h> // bzero()
#include <stdio.h>

#define MEMORY ( 640 * 1048576 )

typedef unsigned char byte;
typedef unsigned short int word;
typedef unsigned long int dword;

class tabmm
{  public:
   void* m[ 0x10 ];
   class tabmm* prev,* next;
   dword loc;

   ~tabmm();
   tabmm();
};

tabmm::~tabmm()
{
}

tabmm::tabmm()
{
   next = prev = NULL;
   int sis = 0;
   for(; sis < 0x10; ++sis )
      m[ sis ] = NULL;
   loc = 0;
}

#define USED ( ( void* )0xffffffff )
#define LOWM ( ( void* )1 )
#define KMEM ( ( void* )2 )
#define INIT ( ( void* )0xfffffffe )
#define KERN 0x109080

class ridp
{  public:
   tabmm* contents[ 4 ];
   ~ridp(){}
   ridp(){}
   tabmm*& operator[]( int indexB );
};

tabmm*& ridp::operator[]( int indexB )
{
   return ( tabmm*& )contents[ indexB ];
}

class rids
{  public:
   ridp* rid;
   ~rids();
   rids();
   rids( ridp* where );
   ridp& operator[]( int indexA );
};

rids::~rids()
{
}

rids::rids()
{
}

rids::rids( ridp* where )
{
   rid = ( ridp* )where;
}

ridp& rids::operator[]( int indexA )
{
   return *( rid + indexA ); // pointer arithmetic to sizeof( ridp );
}

#include <atomic>
byte* memory = NULL;  // for page::dump()

class page
{  public:
   byte data[ 4096 - 456 ];
   byte bmap[ 455 ];
   std::atomic_flag flag;
   ~page();
   page();
   bool bts();
   bool btr();
   bool bts( word loc, byte bit );
   bool btc( word loc, byte bit ); // test and clear
   bool free( void* loc, dword size );
   void* alloc( dword size );
   void clear();
   void dump();
};

page::~page()
{
}

page::page()
{
   bzero( bmap, 255 );
}

bool page::bts()
{
   return flag.test_and_set( std::memory_order_relaxed );
}

bool page::btr()
{
   flag.clear( std::memory_order_relaxed );
   return true;
}

bool page::bts( word loc, byte bit )
{
   bool flag = ( bmap[ loc ] & ( 1 << bit ) ) >> bit;
   if( !flag )
      bmap[ loc ] |= ( 1 << bit );
   return flag;
}

bool page::btc( word loc, byte bit ) // test and clear
{
   bool flag = ( bmap[ loc ] & ( 1 << bit ) ) >> bit;
   if( flag )
   {
//      printf( "%02X", bmap[ loc ] );
      bmap[ loc ] &= ( ( 0xfe << bit ) | ( 0x7f >> ( 7 - bit ) ) );
//      printf( "%02X ", bmap[ loc ] );
   }
   return flag;
}

void page::clear()
{
   word pagesize = sizeof( *this );
   bzero( this, pagesize );
}

#include <sched.h> // sched_yield

bool page::free( void* loc, dword size )
{
   while( bts() )
      sched_yield();
   bool result = true;
   int place = ( byte* )loc - memory;
   int part = place & 0xfff;
   int sis = part / 8;
   int bit = part % 8;
   int end = ( part + size ) / 8;
   int las = ( part + size ) % 8;
//   printf( "%d %d %d %d\r\n", sis, bit, end, las );
   for(; sis < end; ++sis )
   {
//      printf( "-" );
      for(; bit < 8; ++bit )
      {
         bool test = btc( sis, bit );
         result = result && ( !test );
      }
      bit = 0;
   }
   btr();
   return result;
}

// possibly optimize with a byte wide cmpxchg
void* page::alloc( dword size )
{
   while( bts() )
      sched_yield();
   int sis = 0;
   bool there = false;
   void* loc;
   for(; sis < 455; ++sis )//4096 - 456; ++sis )
   {
      if( bmap[ sis ] == 0xff ) // optimized here;
         continue;
      byte bit = 0;
      bool test = false;
      for(; bit < 8; ++bit )
      {
         test = bts( sis, bit );
         if( test )
         {
            continue;
         }
         else break; // bit < 8;
      }
      if( bit == 8 ) continue;
      test = false;
      word offset = sis * 8 + bit;
      byte bit_off = offset % 8;
      word byteoff = offset / 8;
      word top = offset - 1 + size; // we did one bit, offset is +1;
      if( top > 4096 - 456 )
         break; // out of bounds.
      byte bit_top = top % 8;
      word bytetop = top / 8;
      //dump();
      for(;; --bit_top )
      {
         if( bit_top == 0xff )
         {
            bit_top = 8; // becomes 7;
            --bytetop;
            continue;
         }
         if( bytetop == byteoff && bit_top == bit_off )
         {
            test = false;
            break; // this loop
         }
         test = bts( bytetop, bit_top );
         if( test ) break;
      }
      if( !test )
      {
         there = true;
         loc = &data[ offset ];
         break;
      }
   }
   btr();
   if( there )
   {
      bzero( loc, size ); // memory is zeroed in main() and we don't free() yet so.
      return loc;
   }
   return NULL; // butmapped page full
}

void page::dump()
{
   int it = 0;
   for(; it < 455; ++it )
      printf( "%02X", bmap[ it ] );
   printf( "%08X\n", ( dword )( ( byte* )this - memory ) );
}

#define PAGES_KLUDGE_MM ( 0x10 * sizeof( page ) )
#define START_KLUDGE_MM 0x106000 // 0x400000 was a bad decision.
dword page_kludge_mm = START_KLUDGE_MM;

void* alloc_kludge_mm( word size, dword loc )
{
   page* thispage = ( page* )( memory + page_kludge_mm );
   void* attempt = thispage -> alloc( size );
   if( attempt == NULL )
   {
      thispage = ( page* )( memory + ( page_kludge_mm += sizeof( page ) ) );
      thispage -> clear();
      attempt = thispage -> alloc( size );
      if( attempt == NULL )
         printf( "assertion invalid\n" );
   }
   if( loc > 0 )
   {
      ( ( tabmm* )attempt ) -> loc = loc;
   }
   return attempt; // success.
}

#include <strings.h> // bzero()
#include <sched.h> // sched_yield()

#define new_tabmm(x) ( ( tabmm* )alloc_kludge_mm( sizeof( tabmm ), x ) )

page* kmem_param = NULL; // this is where the allocator is working
page* pristine = NULL; // the allocator may usurp this page.  when done, it's back in place elsewhere.
rids rid = NULL; // this is the resident allocations structure at 0x300000

dword calls_tab_alloc_mm = 0;

tabmm* tab_alloc_mm( bool* used ) // internals
{
   tabmm* twign = NULL;
   twign = ( tabmm* )( kmem_param -> alloc( sizeof( tabmm ) ) );
   if( twign == NULL ) // n'what if branchn was full
   {
      (*used) = true;
      kmem_param = pristine;
      twign = ( tabmm* )( kmem_param -> alloc( sizeof( tabmm ) ) );
   }
   while( twign == NULL )
   {
      printf( "stopping..." );
      fflush( stdout );
      sched_yield();
      getchar();
   }
   return twign;
}

tabmm* my_alloc_mm( page* param ) // internals
{
   tabmm* twign = NULL;
   twign = ( tabmm* )( param -> alloc( sizeof( tabmm ) ) );
   if( twign == NULL ) // n'what if branchn was full
   {
      return NULL;
   }
   return twign;
}

tabmm* twig_alloc_mm( page** param, int pid, bool* used, tabmm* prevleaf );
tabmm* branch_alloc_mm( page** param, int pid, bool* used, tabmm* prevtwig );
tabmm* limb_alloc_mm( page** param, int pid, bool* used, tabmm* prevbranch );

// allocate from the list of free leaves
void* leaf_alloc_mm( page** param, word chunk, int pid, bool* used )
{
   tabmm*& _leafn = rid[ 0 ][ 3 ];
   tabmm* leafn = _leafn;
   tabmm* twign = NULL;
   if( leafn == NULL ) leafn = ( tabmm* )INIT;
   byte sis = 0;//, boom = 0; // pages (and leaves);

   void* try1 = NULL;
   if( *param != NULL ) // 0x402000
   {
      if( chunk <= 3640 )
         try1 = (*param) -> alloc( chunk ); // and this is how i lost pristine.  fixed START_KLUDGE_MM.
   }
   if( try1 != NULL )
   {
      return try1;
   }
   int boom = 0;//( leafn -> loc & 0x000f0000 ) >> 0x10; // boom is my twig counter.
   for(; leafn != NULL; ++boom )
   {
      if( leafn != INIT )
      {
         if( leafn == NULL )
         {
            break; // assertion failed.
         }
         boom = ( leafn -> loc & 0x000f0000 ) >> 0x10; // leaf?// boom is my twig counter.
         // scan thru each leaf;
         for( sis = 0; sis < 0x10; ++sis )
         {
            tabmm* pagen = ( tabmm* )( leafn -> m[ sis ] ); // is this part needed?
            if( pagen == NULL ); // new
            else if( ( pagen == USED ) || ( pagen == LOWM ) || ( pagen == KMEM ) || ( ( dword )pagen < ( dword )0x10000 ) ) // is resident
            {
               if( sis < 0x10 ) // sis not yet used.
                  continue;
            }
            // if NULL then allocate one.
            if( pagen == NULL )
            {
               if( *used ) // this code needs to be reusable
               {
                  leafn -> m[ sis ] = KMEM;
                  pristine = kmem_param = ( page* )( memory + leafn -> loc + ( sis << 12 ) ); // find();
                  bzero( kmem_param, sizeof( page ) ); // ?
                  *used = false;
                  calls_tab_alloc_mm = 0;
                  // add to free list;
               }
               else if( kmem_param == pristine )
               {
                  leafn -> m[ sis ] = KMEM;
                  pristine = ( page* )( memory + leafn -> loc + ( sis << 12 ) ); // find();
                  bzero( pristine, sizeof( page ) ); // SIGSEGV
               }
               else // pristine and kmem_param are fresh.
               {
                  if( chunk > 4096 )
                     break;
                  leafn -> m[ sis ] = ( void* )pid; // 3;
                  page* unused = ( page* )( memory + leafn -> loc + ( sis << 12 ) ); // find();
                  unused -> clear();
                  if( chunk > 3640 )
                  {
                     try1 = unused; // no bitmap
                     return try1;
                  }
                  (*param) = unused;
                  try1 = unused -> alloc( chunk ); // SIGSEGV
                  if( try1 == NULL )
                  {
                     break; // assertion failed; oom: out of memory.
                  }
                  return try1;
               }
            }
         }
      }
      else // if( leafn == INIT ); then allocate one leaf. // NOTUSED (haven't seen this run yet)
      {
         printf( "leafn INIT\r\n" ); // this still hasn't happened yet, code may not work.
         leafn = NULL;
         if( twign == NULL )
         {
            twign = twig_alloc_mm( param, pid, used, leafn );
            if( twign == NULL ) break; // oom: out of memory
            boom = 0;
            printf( "%08X no twig\r\n", twign -> loc );
            leafn -> loc = twign -> loc;
         }
         // * main logic: if( twign == INIT ) break; // from twign, ...
         else
         {
            leafn = tab_alloc_mm( used ); // else; fix-0010
         //leafn -> loc = ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 ) + ( boom << 0x10 );
            leafn -> loc = twign -> loc + ( boom << 0x10 );
            boom = ( leafn -> loc & 0x000f0000 ) >> 0x10; // boom is my twig counter.
         }
         //rid[ 0 ][ 2 ] = leafn;
         //rid[ 0 ][ 2 ] = ; // this looks wrong putting a leaf in twig spots
      }
      tabmm* newleaf = leafn -> next;
      if( newleaf == NULL ) // NULL so allocate a leaf
      {  // leafn is a structure.
         if( twign == NULL ) // so far always... 0016
         {
            twign = twig_alloc_mm( param, pid, used, leafn );
            if( twign == NULL ) break; // oom: out of memory
            if( twign == NULL ) break; // oom: out of memory
            if( twign == USED ) break; // oom: out of memory
            if( twign -> m[ 0 ] == USED ) break; // oom: out of memory
            if( twign -> m[ 0 ] == NULL ) break; // oom: out of memory
            newleaf = ( tabmm* )( twign -> m[ 0 ] );
            newleaf -> loc = twign -> loc;
            boom = -1; // -1 if boom increments at the loop
         }
         else
         {
            boom = ( leafn -> loc & 0x000f0000 ) >> 0x10; // boom is my twig counter.
            if( false ) // plausible allocation, search structures
            {
               break;
            }
            newleaf = tab_alloc_mm( used );
            if( newleaf == NULL ) break; // oom: out of memory
            twign -> m[ boom ] = newleaf;
            newleaf -> loc = twign -> loc + ( boom << 0x10 ); // should boom == 0; ? 0016
         }
         newleaf -> prev = leafn;
         leafn -> next = newleaf;
      }
      leafn = leafn -> next;
   }
   if( try1 != NULL ) printf( "...\r\n" );
   return try1; // NULL
}

// allocate from the list of free twigs
tabmm* twig_alloc_mm( page** param, int pid, bool* used, tabmm* prevleaf )
{
   tabmm*& twig = rid[ 0 ][ 2 ];
   tabmm* twign = twig;
   tabmm* leafn = NULL;
   tabmm* branchn = NULL;
   if( twign == NULL ) twign = ( tabmm* )INIT;
   int boom = 0;//, bah = 0; // boom is the leaf counter 0016

   for(; twign != NULL; ) // thus incrementing bah
   {
      if( twign == INIT ) // nontested
      {
         printf( "twign INIT\r\n" );
         twign = NULL;
         //if( branchn == INIT ) break; // out on a limb.
         branchn = branch_alloc_mm( param, pid, used, twign );
         if( branchn == NULL ) break; // oom: out of memory
         twign = tab_alloc_mm( used ); //
         //twign -> loc = ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 );
         twign -> loc = branchn -> loc; // + ( bah << 20 );
         rid[ 0 ][ 2 ] = twign;
      }
      int bah = ( twign -> loc & 0x00f00000 ) >> 20;
      leafn = ( tabmm* )( twign -> m[ boom ] ); // 0x400000
      if( leafn == NULL );
      else if( ( leafn == USED ) || ( leafn == LOWM ) || ( leafn == KMEM ) || ( ( dword )leafn < ( dword )0x10000 ) ) // is resident
      {
         if( ++boom < 0x10 )
            continue;
         else boom = 0; // leaf counter; 0016
      }
      if( leafn == NULL ) // bah == 1
      {
         // fix: create 16 leaves here with tab_alloc_mm( &used )
         int sis = 0;
         for(; sis < 0x10; ++sis )
         {
            leafn = tab_alloc_mm( used );
            leafn -> loc = ( twign -> loc ) + ( sis << 0x10 );
            if( prevleaf ) prevleaf -> next = leafn;
            leafn -> prev = prevleaf;
            prevleaf = leafn;
            twign -> m[ sis ] = leafn; // [ boom ] is out of range?
         }
         leafn = ( tabmm* )( twign -> m[ 0 ] );
         return twign; // always seems to be 0x500000
      }
      if( twign == NULL );// break; // 0016
      else if( ( twign == USED ) || ( twign == LOWM ) || ( twign == KMEM ) || ( ( dword )twign < ( dword )0x10000 ) ) // is resident
      {
         boom = 0; // continue;?? 0016
      }
      tabmm* newtwig = twign -> next;
      if( newtwig == NULL ) // so allocate a twig
      {  // twign is a structure.
         if( branchn == NULL )
         {
            branchn = branch_alloc_mm( param, pid, used, twign );
            if( branchn == NULL ) break; // oom: out of memory
            if( branchn == USED ) break; // oom: out of memory
            if( branchn -> m[ 0 ] == USED ) break; // oom: out of memory
            if( branchn -> m[ 0 ] == NULL ) break; // oom: out of memory
            bah = 0;
            newtwig = ( tabmm* )( branchn -> m[ 0 ] );
            boom = 0; // -1 if boom increments
         }
         else
         {
            newtwig = tab_alloc_mm( used ); //
            if( newtwig == NULL ) break; // oom: out of memory
            branchn -> m[ bah ] = newtwig;
         }
         newtwig -> loc = branchn -> loc;// + ( bah << 20 ); // fix-0012
         newtwig -> prev = twign;
         twign -> next = newtwig;
      }
      twign = twign -> next;
   }
   return NULL;
}

// allocate from the list of free branches
tabmm* branch_alloc_mm( page** param, int pid, bool* used, tabmm* prevtwig )
{
   tabmm*& branch = rid[ 0 ][ 1 ];
   tabmm* branchn = branch;
   tabmm* twign = NULL;
   tabmm* limbn = NULL;
   if( branchn == NULL ) branchn = ( tabmm* )INIT;
   int bah = 0, faz = 0;

   bah = 0;
   for( faz = 0; branchn != NULL; ++bah ) // incrementing faz too
   {
      faz = ( branchn -> loc & 0x0f000000 ) >> 24;
      twign = ( tabmm* )( branchn -> m[ bah ] );
      if( twign == NULL );
      else if( ( twign == USED ) || ( twign == LOWM ) || ( twign == KMEM ) || ( ( dword )twign < ( dword )0x10000 ) ) // is resident
      {
         if( bah < 0x10 )
            continue;
      }
      if( branchn == INIT ) // nontested, probably doesn't work
      {  // branchn is a structure, not the location it represents.
         branchn = ( tabmm* )( kmem_param -> alloc( sizeof( tabmm ) ) );
         if( branchn == NULL )
         {
            *used = true;
            kmem_param = pristine;
            branchn = ( tabmm* )( kmem_param -> alloc( sizeof( tabmm ) ) );
         }
         branchn -> loc = limbn -> loc + ( faz << 24 );
         rid[ 0 ][ 1 ] = branchn;
      }
      twign = ( tabmm* )( branchn -> m[ bah ] );
      if( twign == NULL )
      {
         int sis = 0;
            for(; sis < 0x10; ++sis )
            {
               twign = tab_alloc_mm( used );
               twign -> loc = ( branchn -> loc ) + ( sis << 20 );
               if( prevtwig ) prevtwig -> next = twign;
               twign -> prev = prevtwig;
               prevtwig = twign;
               branchn -> m[ sis ] = twign;
            }
            return branchn;
      }
      bah = 0; // 0002
      tabmm* newbranch = branchn -> next;
      if( newbranch == NULL ) // if branchn -> next == NULL then allocate one.
      {
         if( limbn == NULL )
         {
            limbn = limb_alloc_mm( param, pid, used, branchn );
            if( limbn == USED ) break; // oom: out of memory
            if( limbn == NULL ) break; // oom: out of memory
            if( limbn -> m[ 0 ] == USED ) break; // oom: out of memory
            if( limbn -> m[ 0 ] == NULL ) break; // oom: out of memory
            newbranch = ( tabmm* )( limbn -> m[ 0 ] );
            newbranch -> loc = limbn -> loc;// + ( faz << 24 ); // fix-0009
            faz = -1; // catch that faz increments
         }
         else // limbn is valid.
         {
            if( limbn -> m[ faz ] == USED ) break;
            newbranch = tab_alloc_mm( used ); // fix-0008
            if( newbranch == NULL ) break; // oom: out of memory
            newbranch -> loc = ( limbn -> loc ); // ( 1 << 24 ) fix-0012
            limbn -> m[ faz ] = newbranch;
         }
         newbranch -> prev = branchn;
         branchn -> next = newbranch;
      }
      else
      branchn = newbranch;//branchn -> next;
   }
   return NULL;
}

bool leaf_add( tabmm* leafn )
{
   tabmm* leaf = ( tabmm* )( rid[ 0 ][ 0 ] );
   for(; leaf -> next != NULL; leaf = leaf -> next );
   leafn -> prev = leaf;
   leaf -> next = leafn;
   int faz = 0;
   for(; faz < 0x10; ++faz )
   {
      leafn -> m[ faz ] = NULL;
   }
   return true;
}

bool twig_add( tabmm* twign )
{
   tabmm* twig = ( tabmm* )( rid[ 0 ][ 2 ] );
   for(; twig -> next != NULL; twig = twig -> next );
   twign -> prev = twig;
   twig -> next = twign;
   int faz = 0;
   for(; faz < 0x10; ++faz )
   {
      twign -> m[ faz ] = NULL;
   }
   return true;
}

bool branch_add( tabmm* branchn )
{
   tabmm* branch = ( tabmm* )( rid[ 0 ][ 1 ] );
   for(; branch -> next != NULL; branch = branch -> next );
   branchn -> prev = branch;
   branch -> next = branchn;
   int faz = 0;
   for(; faz < 0x10; ++faz )
   {
      branchn -> m[ faz ] = NULL;
   }
   return true;
}

bool limb_add( tabmm* limbn )
{
   tabmm* limb = ( tabmm* )( rid[ 0 ][ 0 ] );
   for(; limb -> next != NULL; limb = limb -> next );
   limbn -> prev = limb;
   limb -> next = limbn;
   int faz = 0;
   for(; faz < 0x10; ++faz )
   {
      limbn -> m[ faz ] = NULL;
   }
   return true;
}

void print_mm( const char* message, tabmm* tab )
{
   if( tab == NULL )
      printf( "NULL" );
   else if( tab == USED )
      printf( "USED" );
   else if( tab == INIT )
      printf( "INIT" );
   else if( ( dword )tab < ( dword )0x10000 )
      printf( "    %04X", tab );
   else
   {
      printf( "%08X, ", ( dword )tab - ( dword )memory ); // USED
      printf( "%08X", ( dword )( tab -> loc ) ); // USED
   }
   printf( ": " );
   puts( message );
}

tabmm* trunk = NULL;

// can we mutex the allocator for this?
void* at_alloc_mm( void* place, dword size, int pid, page* klpage )
{
   tabmm* limbn = NULL,* branchn = NULL,* twign = NULL,* leafn;
   void* pagen = NULL;
   dword plac = ( dword )( ( byte* )place - memory );
   int sis = ( plac & 0x0000f000 ) >> 0xc; // starts at 0.
   int boom = ( plac & 0x000f0000 ) >> 0x10; // starts at 0.
   int bah = ( plac & 0x00f00000 ) >> 20; // starts at 0.
   int faz = ( plac & 0x0f000000 ) >> 24; // starts at 0.
   int meh = ( plac & 0xf0000000 ) >> 28;
   if( pid > 0xffff ) return NULL; // otherwise the rid table is >1MiB
   dword end = ( plac + size );
   if( ( end & 0x00000fff ) > 0 ) // asserting this.
   {
      //printf( "shaving the end\r\n" );
      end &= 0xfffff000;
      end += 0x00001000;
   }
   for(; meh < 0x10; ++meh ) // top.
   {
      limbn = ( tabmm* )( trunk -> m[ meh ] );
//print_mm( "limb", limbn );
//      faz = 0;
      if( limbn == NULL )
      {
         if( end > ( meh + 1 ) << 28 )
         {
            trunk -> m[ meh ] = ( void* )pid;
            continue;
         }
         if( end == ( meh + 1 ) << 28 )
         {
            trunk -> m[ meh ] = ( void* )pid;
            return ( byte* )( ( dword )plac + ( dword )memory );
         }
         else
         {
            //prealloc a kludge somewhere; because none of this is available
            if( klpage == NULL )
            {
               dword klsize = ( ( meh + 1 ) << 28 ) - end;
               dword klplac = end;
               klpage = ( page* )klplac;
               klpage -> clear();
            }
            limbn = my_alloc_mm( klpage );
            limbn -> loc = ( meh << 28 );
            trunk -> m[ meh ] = limbn;
            limb_add( limbn ); // alloc substructure
         }
         for(; faz < 0x10; ++faz )
         {
//            limbn = ( tabmm* )( trunk -> m[ meh ] );
            branchn = ( tabmm* )( trunk -> m[ meh ] ); // will segv
//print_mm( "lb", branchn );
            if( end > ( ( meh ) << 28 ) + ( ( faz + 1 ) << 24 ) )
            {
               limbn -> m[ faz ] = ( void* )pid;
//printf( "%d %d %d %d %d %08X+\r\n", meh, faz, bah, boom, sis, end );
//printf( "end: %08X %08X\r\n", end, ( meh << 28 ) + ( faz + 1 ) << 24 );
               continue;
            }
//            bah = 0;
            if( end == ( ( meh ) << 28 ) + ( ( faz + 1 ) << 24 ) )
            {
               limbn -> m[ faz ] = ( void* )pid;
               return ( byte* )( ( dword )plac + ( dword )memory );
            }
            else
            {
               //prealloc a kludge somewhere; because none of this is available
               branchn = my_alloc_mm( klpage );
               branchn -> loc = ( meh << 28 ) + ( faz << 24 );
               ( ( tabmm* )limbn ) -> m[ faz ] = branchn;
               branch_add( branchn ); // alloc substructure
            }
            for(; bah < 0x10; ++bah )
            {
               twign = ( tabmm* )( branchn -> m[ bah ] );
//print_mm( "lbt", twign );
               if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah + 1 ) << 20 ) )
               {
                  branchn -> m[ bah ] = ( void* )pid;
                  continue;
               }
//               boom = 0;
               if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah + 1 ) << 20 ) )
               {
                  branchn -> m[ bah ] = ( void* )pid;
                  return ( byte* )( ( dword )plac + ( dword )memory );
               }
               else
               {
                  //prealloc a kludge somewhere; because none of this is available
                  twign = my_alloc_mm( klpage );
                  twign -> loc = ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 );
                  ( ( tabmm* )( branchn ) ) -> m[ bah ] = twign;
                  twig_add( twign ); // alloc substructure
               }
               for(; boom < 0x10; ++boom )
               {
                  leafn = ( tabmm* )( twign -> m[ boom ] );
//print_mm( "lbtl", leafn );
                  if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom + 1 ) << 0x10 ) )
                  {
                     twign -> m[ boom ] = ( void* )pid;
                     continue;
                  }
//                  sis = 0;
                  if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom + 1 ) << 0x10 ) )
                  {
                     twign -> m[ boom ] = ( void* )pid;
                     return ( byte* )( ( dword )plac + ( dword )memory );
                  }
                  else
                  {
                     //prealloc a kludge somewhere; because none of this is available
                     leafn = my_alloc_mm( klpage );
                     leafn -> loc = ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 ) + ( boom << 0x10 );
                     ( ( tabmm* )( twign ) ) -> m[ boom ] = leafn;
                     leaf_add( leafn ); // alloc substructure
                  }
                  for(; sis < 0x10; ++sis )
                  {
                     pagen = ( tabmm* )( leafn -> m[ sis ] );
//print_mm( "lbtlp", ( tabmm* )pagen );
                     if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc ) )
                     {
                        leafn -> m[ sis ] = ( void* )pid;
                        continue;
                     }
                     if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc ) )
                     {
                        leafn -> m[ sis ] = ( void* )pid;
                        return ( byte* )( ( dword )plac + ( dword )memory );
//                        break;
                     }
                     else
                     {
                        printf( "assertion failed\r\n" );
                     }
                  }
                  sis = 0;
               }
               boom = 0;
            }
            bah = 0;
         }
         faz = 0;
      }
      else if( limbn == USED || limbn == LOWM || limbn == KMEM || ( ( dword )limbn < ( dword )0x10000 ) ) // is resident
         return NULL; // fixme: deallocate
      else for(; faz < 0x10; ++faz ) // count branches
      {
         branchn = ( tabmm* )( limbn -> m[ faz ] );
//print_mm( "branch", branchn );
//         bah = 0;
         if( branchn == NULL )
         {
//            branchn = ( tabmm* )( trunk -> m[ meh ] ); // will segv
            if( end > ( ( meh ) << 28 ) + ( ( faz + 1 ) << 24 ) )
            {
//printf( "/%X/a", faz );
               limbn -> m[ faz ] = ( void* )pid;
               continue;
            }
//            bah = 0;
            if( end == ( ( meh ) << 28 ) + ( ( faz + 1 ) << 24 ) )
            {
//printf( "/%X/s", faz );
               limbn -> m[ faz ] = ( void* )pid;
               return ( byte* )( ( dword )plac + ( dword )memory );
            }
            else
            {
               //prealloc a kludge somewhere; because none of this is available
               if( klpage == NULL )
               {
                  dword klsize = ( meh << 28 ) + ( ( faz + 1 ) << 20 ) - end;
                  dword klplac = end;
                  klpage = ( page* )klplac;
                  klpage -> clear();
               }
               branchn = my_alloc_mm( klpage );
               branchn -> loc = ( meh << 28 ) + ( faz << 24 );
               ( ( tabmm* )limbn ) -> m[ faz ] = branchn;
               branch_add( branchn ); // alloc substructure
            }
            for(; bah < 0x10; ++bah )
            {
               twign = ( tabmm* )( branchn -> m[ bah ] );
//print_mm( "bt", twign );
               if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah + 1 ) << 20 ) )
               {
//printf( "\\" );
                  branchn -> m[ bah ] = ( void* )pid;
                  continue;
               }
//               boom = 0;
               if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah + 1 ) << 20 ) )
               {
//printf( "=t" );
                  branchn -> m[ bah ] = ( void* )pid;
                  return ( byte* )( ( dword )plac + ( dword )memory );
               }
               else // if( twign == NULL )
               {
                  //prealloc a kludge somewhere; because none of this is available
                  twign = my_alloc_mm( klpage );
                  twign -> loc = ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 );
//printf( "/" );
                  ( ( tabmm* )( branchn ) ) -> m[ bah ] = twign;
                  twig_add( twign ); // alloc substructure
               }
               for(; boom < 0x10; ++boom )
               {
                  leafn = ( tabmm* )( twign -> m[ boom ] );
//print_mm( "btl", leafn );
                  if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom + 1 ) << 0x10 ) )
                  {
                     twign -> m[ boom ] = ( void* )pid;
                     continue;
                  }
//                  sis = 0;
                  if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom + 1 ) << 0x10 ) )
                  {
//printf( "=l" );
                     twign -> m[ boom ] = ( void* )pid;
                     return ( byte* )( ( dword )plac + ( dword )memory );
                  }
                  else
                  {
                     //prealloc a kludge somewhere; because none of this is available
                     leafn = my_alloc_mm( klpage );
                     leafn -> loc = ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 ) + ( boom << 0x10 );
                     ( ( tabmm* )( twign ) ) -> m[ boom ] = leafn;
                     leaf_add( leafn ); // alloc substructure
                  }
                  for(; sis < 0x10; ++sis )
                  {
                     pagen = ( tabmm* )( leafn -> m[ sis ] );
//print_mm( "btlp", ( tabmm* )pagen );
                     if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc ) )
                     {
                        leafn -> m[ sis ] = ( void* )pid;
                        continue;
                     }
                     if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc ) )
                     {
/*
printf( "\r\n" );
printf( "*** report:" );
fflush( stdout );
{
   int m = 1;  //tabmm* _m;
   //if( ( dword )_m < ( dword )0x10000 ) continue;
   //if( _m == USED ) continue;
   for(; m < 0x10; ++m )
   {
      int z = 0;  tabmm* _z;
      printf( "%X ", m );
      print_mm( "limbn", _z = ( tabmm* )( trunk -> m[ m ] ) );
fflush( stdout );
      if( ( dword )_z < ( dword )0x10000 ) continue;
      if( _z == USED ) continue; // n'why was limb 2 USED ? 0018
      for(; z < 0x10; ++z )
      {
         int h = 0;  tabmm* _h;
         printf( "%X %X ", m, z );
         print_mm( "branchn", _h = ( tabmm* )( _z -> m[ z ] ) );
fflush( stdout );
         if( ( dword )_h < ( dword )0x10000 ) continue;
         if( _h == USED ) continue;
         for(; h < 0x10; ++h )
         {
            int b = 0; tabmm* _b;
            printf( "%X %X %X ", m, z, h );
            print_mm( "twign", _b = ( tabmm* )( _h -> m[ h ] ) );
fflush( stdout );
            if( ( dword )_b < ( dword )0x10000 ) continue;
            if( _b == USED ) continue;
            for(; b < 0x10; ++b )
            {
               int s = 0;
               for(; s < 0x10; ++s )
               {
               }
            }
         }
      }
   }
}
printf( "%08X=p ", ( dword )plac ); // reports here
*/
                        leafn -> m[ sis ] = ( void* )pid;
                        return ( byte* )( ( dword )plac + ( dword )memory );
//                        break;
                     }
                     else
                     {
                        printf( "assertion failed\r\n" );
                     }
                  }
                  sis = 0;
               }
               boom = 0;
            }
            bah = 0;
         }
         else if( branchn == USED || branchn == LOWM || branchn == KMEM || ( ( dword )branchn < ( dword )0x10000 ) ) // is resident
            return NULL; // fixme
         else for(; bah < 0x10; ++bah ) // populated substructure
         {
            twign = ( tabmm* )( branchn -> m[ bah ] );
//print_mm( "twig", twign );
//            boom = 0;
//print_mm( "bah", twign );
//printf( "%d %d %d %d %d %08X+\r\n", meh, faz, bah, boom, sis, end );
            if( twign == NULL )
            {
//               branchn = ( tabmm* )( limbn -> m[ faz ] );
               if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah + 1 ) << 20 ) )
               {
                  branchn -> m[ bah ] = ( void* )pid;
                  continue;
               }
//               boom = 0;
               if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah + 1 ) << 20 ) )
               {
                  branchn -> m[ bah ] = ( void* )pid;
                  return ( byte* )( ( dword )plac + ( dword )memory );
               }
               else
               {
                  //prealloc a kludge somewhere; because none of this is available
                  if( klpage == NULL )
                  {
                     dword klsize = ( meh << 28 ) + ( ( faz + 1 ) << 20 ) - end;
                     dword klplac = end;
                     klpage = ( page* )klplac;
                     klpage -> clear();
                  }
                  twign = my_alloc_mm( klpage );
                  twign -> loc = ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 );
                  ( ( tabmm* )( branchn ) ) -> m[ bah ] = twign;
                  twig_add( twign ); // alloc substructure
               }
               for(; boom < 0x10; ++boom )
               {
                  leafn = ( tabmm* )( twign -> m[ boom ] );
//print_mm( "tl", leafn );
//printf( "%d ", boom );
//print_mm( "boom twign", twign );
                  if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom + 1 ) << 0x10 ) )
                  {
                     twign -> m[ boom ] = ( void* )pid;
                     continue;
                  }
//                  sis = 0;
                  if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom + 1 ) << 0x10 ) )
                  {
                     twign -> m[ boom ] = ( void* )pid;
                     return ( byte* )( ( dword )plac + ( dword )memory );
                  }
                  else
                  {
                     //prealloc a kludge somewhere; because none of this is available
                     leafn = my_alloc_mm( klpage );
                     leafn -> loc = ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 ) + ( boom << 0x10 );
                     ( ( tabmm* )( twign ) ) -> m[ boom ] = leafn;
                     leaf_add( leafn ); // alloc substructure
                  }
                  for(; sis < 0x10; ++sis )
                  {
                     pagen = ( tabmm* )( leafn -> m[ sis ] );
//print_mm( "tlp", ( tabmm* )pagen );
                     if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc ) )
                     {
                        leafn -> m[ sis ] = ( void* )pid;
                        continue;
                     }
                     if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc ) )
                     {
                        leafn -> m[ sis ] = ( void* )pid;
                        return ( byte* )( ( dword )plac + ( dword )memory );
                        break;
                     }
                     else
                     {
                        printf( "assertion failed\r\n" );
                     }
                  }
                  sis = 0;
               }
               boom = 0;
            }
            else if( twign == USED || twign == LOWM || twign == KMEM || ( ( dword )twign < ( dword )0x10000 ) ) // is resident
            {
//printf( "catching up...\r\n" );
               return NULL; // fixme
            }
            else for(; boom < 0x10; ++boom )
            {
               //print_mm( "twign", twign );
               leafn = ( tabmm* )( twign -> m[ boom ] );
//print_mm( "leaf", leafn );
//               print_mm( "leafn NULL ?", leafn );
//               sis = 0;
               if( leafn == NULL )
               {
//                  twign = ( tabmm* )( branchn -> m[ bah ] );
//print_mm( "+ twign", twign );
                  if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom + 1 ) << 0x10 ) )
                  {
//print_mm( "set boom", twign );
                     twign -> m[ boom ] = ( void* )pid;
                     continue;
                  }
//                  sis = 0;
                  if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom + 1 ) << 0x10 ) )
                  {
                     twign -> m[ boom ] = ( void* )pid;
                     return ( byte* )( ( dword )plac + ( dword )memory );
                  }
                  else
                  {
                     //prealloc a kludge somewhere; because none of this is available
                     if( klpage == NULL )
                     {
                        dword klsize = ( meh << 28 ) + ( ( faz + 1 ) << 20 ) - end;
                        dword klplac = end;
                        klpage = ( page* )klplac;
                        klpage -> clear();
                     }
                     leafn = my_alloc_mm( klpage );
//if( leafn == NULL )
//printf( "assertion\r\n" );
//print_mm( "+ leafn", leafn );
//printf( "%d %d %d %d %d +\r\n", meh, faz, bah, boom, sis );
                     leafn -> loc = ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 ) + ( boom << 0x10 );
                     ( ( tabmm* )( twign ) ) -> m[ boom ] = leafn;
                     leaf_add( leafn ); // alloc substructure
//print_mm( "- leafn", leafn );
                  }
                  for(; sis < 0x10; ++sis )
                  {
                     pagen = ( tabmm* )( leafn -> m[ sis ] );
//print_mm( "lp", ( tabmm* )pagen );
//print_mm( "intermediate leafn", leafn );
                     if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc ) )
                     {
                        leafn -> m[ sis ] = ( void* )pid;
                        continue;
                     }
                     if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc ) )
                     {
                        leafn -> m[ sis ] = ( void* )pid;
                        return ( byte* )( ( dword )plac + ( dword )memory );
                        break;
                     }
                     else // yet if we passed the end?
                     {
                        printf( "0002 %08X assertion failed\r\n", end );
                     }
                  }
                  sis = 0;
               }
               else if( leafn == USED || leafn == LOWM || leafn == KMEM || ( ( dword )leafn < ( dword )0x10000 ) ) // is resident
                  return NULL; // fixme
               //else
               for(; sis < 0x10; ++sis )
               {
                  //print_mm( "leafn", leafn );
                  pagen = leafn -> m[ sis ]; // void*
//print_mm( "page", ( tabmm* )pagen );
                  //printf( "%d %d %d %d %d\r\n", meh, faz, bah, boom, sis );
                  //printf( "%08X %08X pagen\n", ( dword )pagen - ( dword )memory, ( dword )pagen );
                  if( pagen == NULL )
                  {
                     //printf( "x\n" );
                     //print_mm( "twig", twign );
                     //print_mm( "leafn", leafn );
                     leafn = ( tabmm* )( twign -> m[ boom ] );
                     if( end > ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc ) )
                     {
                        leafn -> m[ sis ] = ( void* )pid;
                     //print_mm( "cont twig", twign );
                     //print_mm( "cont leafn", leafn );
                        continue;
                     }
                     if( end == ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc ) )
                     {
                        leafn -> m[ sis ] = ( void* )pid;
                     //print_mm( "break twig", twign );
                     //print_mm( "break leafn", leafn );
                        return ( byte* )( ( dword )plac + ( dword )memory );
                        break;
                     }
                     else
                     {
//                        printf( "assertion failed\r\n" );
                        printf( "0001 %08X assertion failed\r\n", end );
                        return NULL;
                     }
                     //print_mm( "past twig", twign );
                     //print_mm( "past leafn", leafn );
                  }
                  //else printf( "pagen != NULL\r\n" );
//                  else start = ( ( meh ) << 20 ) + ( faz << 24 ) + ( bah << 20 ) + ( boom << 0x10 ) + ( ( sis + 1 ) << 0xc );
               }
               sis = 0;
            }
            //printf( "boom = 0\r\n" );
            boom = 0;
         }
         bah = 0;
      }
      faz = 0;
   }
   return NULL;
}

bool report_mm()
{
printf( "\r\n" );
printf( "*** report:" );
fflush( stdout );
{
   int m = 0;//1;  //tabmm* _m;
   //if( ( dword )_m < ( dword )0x10000 ) continue;
   //if( _m == USED ) continue;
   for(; m < 0x10; ++m )
   {
      int z = 0;  tabmm* _z;
      printf( "%X ", m );
      print_mm( "limbn", _z = ( tabmm* )( trunk -> m[ m ] ) );
fflush( stdout );
      if( ( dword )_z < ( dword )0x10000 ) continue;
      if( _z == USED ) continue; // n'why was limb 2 USED ? 0018
      for(; z < 0x10; ++z )
      {
         int h = 0;  tabmm* _h;
         printf( "%X %X ", m, z );
         print_mm( "branchn", _h = ( tabmm* )( _z -> m[ z ] ) );
fflush( stdout );
         if( ( dword )_h < ( dword )0x10000 ) continue;
         if( _h == USED ) continue;
         for(; h < 0x10; ++h )
         {
            int b = 0; tabmm* _b;
            printf( "%X %X %X ", m, z, h );
            print_mm( "twign", _b = ( tabmm* )( _h -> m[ h ] ) );
fflush( stdout );
            if( ( dword )_b < ( dword )0x10000 ) continue;
            if( _b == USED ) continue;
            for(; b < 0x10; ++b )
            {
               int s = 0;
               for(; s < 0x10; ++s )
               {
               }
            }
         }
      }
   }
}
   return true;
}

void* pages_count_mm( dword chunk, dword* size )
{
   tabmm* pagen = NULL;
   tabmm* limbn = NULL;//rid[ 0 ][ 0 ];
   tabmm* branchn = NULL;//rid[ 0 ][ 1 ];
   tabmm* twign = NULL;//rid[ 0 ][ 2 ];
   tabmm* leafn = rid[ 0 ][ 3 ];
   int faz = ( leafn -> loc & 0x0f000000 ) >> 24; // starts at 0.
   int bah = ( leafn -> loc & 0x00f00000 ) >> 20; // starts at 0.
   int boom = ( leafn -> loc & 0x000f0000 ) >> 0x10; // starts at 0.
   int sis = ( leafn -> loc & 0x0000f000 ) >> 0xc; // starts at 0.
   dword end = 0, start = leafn -> loc;
   *size = 0;
   int meh = 0;
   for(; meh < 0x10; ++meh ) // top.
   {
//printf( "\'" );
      limbn = ( tabmm* )( trunk -> m[ meh ] );
      if( limbn == NULL )
      {
         end = ( meh + 1 ) << 28;
         if( end - start >= chunk )
         {
            *size = chunk;
//            printf( "returned a limb.\r\n" );
            return ( void* )( start + ( dword )memory );
         }
      }
      else if( limbn == USED || limbn == LOWM || limbn == KMEM || ( ( dword )limbn < ( dword )0x10000 ) ) // is resident
      {
//printf( "\"" );
         start = ( meh + 1 ) << 28;
      }
      else for(; faz < 0x10; ++faz ) // count branches
      {
//printf( "-" );
         branchn = ( tabmm* )( limbn -> m[ faz ] );
         if( branchn == NULL )
         {
            end = ( ( meh ) << 28 ) + ( ( faz + 1 ) << 24 );
            if( end - start >= chunk )
            {
               *size = chunk;
//               printf( "returned a branch.\r\n" );
               return ( void* )( start + ( dword )memory );
            }
         }
         else if( branchn == USED || branchn == LOWM || branchn == KMEM || ( ( dword )branchn < ( dword )0x10000 ) ) // is resident
         {
            start = ( ( meh ) << 28 ) + ( ( faz + 1 ) << 24 );
//printf( "_(%08X)", start );
         }
         else for(; bah < 0x10; ++bah ) // populated substructure
         {
//printf( "." );
            //printf( "%d %d\r\n", start, end );
            twign = ( tabmm* )( branchn -> m[ bah ] );
            if( twign == NULL )
            {
               end = ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah + 1 ) << 20 );
               //printf( "f %d %d\r\n", start, end );
               if( end - start >= chunk )
               {
                  //printf( "y %d %d\r\n", start, end );
                  *size = chunk;
//                  printf( "returned a twig.\r\n" );
                  return ( void* )( start + ( dword )memory );
               }
            }
            else if( twign == USED || twign == LOWM || twign == KMEM || ( ( dword )twign < ( dword )0x10000 ) ) // is resident
            {
//printf( "," );
               start = ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah + 1 ) << 20 );
               //printf( "x %d %d\r\n", start, end );
            }
            else for(; boom < 0x10; ++boom )
            {
//printf( "+" );
               leafn = ( tabmm* )( twign -> m[ boom ] );
               //printf( "h %d %d\r\n", start, end );
               if( leafn == NULL )
               {
                  end = ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom + 1 ) << 0x10 );
                  //printf( "s %d %d\r\n", start, end );
                  if( end - start >= chunk )
                  {
                     *size = chunk;
//                     printf( "returned a leaf.\r\n" );
                     return ( void* )( start + ( dword )memory );
                  }
               }
               else if( leafn == USED || leafn == LOWM || leafn == KMEM || ( ( dword )leafn < ( dword )0x10000 ) ) // is resident
               {
//printf( "*" );
                  start = ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom + 1 ) << 0x10 );
                  //printf( "x %d %d\r\n", start, end );
               }
               else for(; sis < 0x10; ++sis )
               {
//printf( "x" );
                  pagen = ( tabmm* )( leafn -> m[ sis ] );
                  //printf( "t %08X %08X\r\n", start, end );
                  if( pagen == NULL )
                  {
                     end = ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc );
                     if( end - start >= chunk )
                     {
                        *size = chunk;
//                        printf( "returned a page.\r\n" );
                        return ( void* )( start + ( dword )memory );
                     }
                  }
                  else //if( pagen == USED || pagen == LOWM || pagen == KMEM || ( ( dword )pagen < ( dword )0x10000 ) ) // is resident
                  {
//printf( "X" );
                     //printf( "a %d %d\r\n", start, end );
                     start = ( ( meh ) << 28 ) + ( ( faz ) << 24 ) + ( ( bah ) << 20 ) + ( ( boom ) << 0x10 ) + ( ( sis + 1 ) << 0xc );
                  }
               }
               sis = 0;
            }
            boom = 0;
         }
         bah = 0;
      }
      faz = 0;
   }
   return 0;
}

/*
void* branch_count_mm( page** param, dword chunk, int* size, bool* used )
{
   tabmm* branchn = rid[ 0 ][ 1 ];
   tabmm* limbn = rid[ 0 ][ 0 ];
   int faz = ( branchn -> loc & 0x0f000000 ) >> 24; // starts at 0.
   dword end = 0, start = 0;
   *size = 0;
   int meh = 0;
   for(; limbn != NULL; ++meh ) // top.
   {
      limbn = ( tabmm* )( trunk -> m[ meh ] );
      if( limbn == NULL )
         ;
      else if( limbn == USED || limbn == LOWM || limbn == KMEM || ( ( dword )limbn < ( dword )0x10000 ) ) // is resident
         ;
      else for(; branchn != NULL; ++faz ) // count branches
      {
         branchn = ( tabmm* )( limbn -> m[ faz ] );
         if( branchn == NULL )
         {
            end = ( ( meh ) << 28 ) + ( faz + 1 ) << 24;
            if( end - start >= chunk )
            {
               *size = start + chunk;
               return ( void* )( start + ( dword )memory );
            }
         }
         else if( branchn == USED || branchn == LOWM || branchn == KMEM || ( ( dword )branchn < ( dword )0x10000 ) ) // is resident
         {
            start = ( ( meh ) << 28 ) + ( faz + 1 ) << 24;
            //continue;
         }
         else // populated substructure
         {
            start = ( ( meh ) << 28 ) + ( faz + 1 ) << 24;
         }
      }
   }
   return 0;
}

void* limb_count_mm( page** param, dword chunk, int* size, bool* used )
{
   tabmm* limbn = rid[ 0 ][ 0 ];
   int meh = ( limbn -> loc & 0xf0000000 ) >> 28; // starts at 0.
   dword end = 0, start = 0;
   *size = 0;
   for(; limbn != NULL; ++meh ) // top.
   {
      limbn = ( tabmm* )( trunk -> m[ meh ] );
      if( limbn == NULL )
      {
         end = ( meh + 1 ) << 28;
         if( end - start >= chunk )
         {
            *size = start + chunk;
            return ( void* )( start + ( dword )memory );
         }
      }
      else if( limbn == USED || limbn == LOWM || limbn == KMEM || ( ( dword )limbn < ( dword )0x10000 ) ) // is resident
      {
         start = ( meh + 1 ) << 28;
         //if( meh < 0x10 )
            continue;
         //else break;
      }
      else // populated substructure
      {
         start = ( meh + 1 ) << 28;
      }
   }
   return 0;
}
*/

// allocate from the list of free limbs (the trunk)
tabmm* limb_alloc_mm( page** param, int pid, bool* used, tabmm* prevbranch )
{
   tabmm*& limb = rid[ 0 ][ 0 ];
   tabmm* limbn = limb;
   tabmm* branchn = NULL;
   if( limbn == NULL ) limbn = ( tabmm* )INIT;
   int faz = 0, meh = 0; // count branches and limbs.

   faz = 0;
   for( meh = 0; limbn != NULL; ++faz ) // top.
   {
      meh = ( limbn -> loc & 0xf0000000 ) >> 28; // starts at 0.
      branchn = ( tabmm* )( limbn -> m[ faz ] );
      if( branchn == NULL );
      else if( branchn == USED || branchn == LOWM || branchn == KMEM || ( ( dword )branchn < ( dword )0x10000 ) ) // is resident
      {
         if( faz < 0x10 )
            continue;
         else faz = -1;
      }
      if( faz >= 0 )
         branchn = ( tabmm* )( limbn -> m[ faz ] );
      else branchn = NULL;
         bool nbr = false;
         if( branchn == NULL ) // meh == 2, faz == 0 (?) ~0014
         {
            int sis = 0; // parcel out a branch and return the limb.
            for(; sis < 0x10; ++sis )
            {
               branchn = ( tabmm* )( limbn -> m[ sis ] );
               if( branchn == NULL );
               else if( branchn == USED || branchn == LOWM || branchn == KMEM || ( dword )branchn < ( dword )0x10000 ) // is resident
               {
                  continue;
               }
               else // branchn != NULL // 0017
               {
                  continue; // substructure
               }
               nbr = true;
               branchn = tab_alloc_mm( used ); // 0004 ???
               branchn -> loc = ( limbn -> loc ) + ( sis << 24 );
               if( prevbranch ) prevbranch -> next = branchn;
               branchn -> prev = prevbranch;
               prevbranch = branchn;
               limbn -> m[ sis ] = branchn;
            }
            if( nbr ) return limbn;
         }
      ++meh;
      if( meh == 0x10 ) break; // oom: out of memory // limbn won't be NULL.
      tabmm* newlimb = limbn -> next;
      if( newlimb == NULL )
      {
         tabmm* limbo = NULL;
         for( meh = 0; meh < 0x10; ++meh )
         {
            limbo = ( tabmm* )( trunk -> m[ meh ] );
            if( limbo == NULL )
               break;
            else if( limbo == USED || limbo == LOWM || limbo == KMEM || ( ( dword )limbo < ( dword )0x10000 ) ) // is resident
               ;
         }
         if( meh == 0x10 ) break; // oom: out of memory
         newlimb = tab_alloc_mm( used );
         if( newlimb == NULL )
         {
            *used = true;
            kmem_param = pristine;
            newlimb = tab_alloc_mm( used ); //limbn =;
         }
         newlimb -> loc = ( meh << 28 );
         newlimb -> prev = limbn;
         trunk -> m[ meh ] = newlimb;
         limbn -> next = newlimb;
      }
      limbn = limbn -> next;
   }
   return NULL;
}

bool used = false;

// more() : sanitize allocation requests
void* more( page** param, dword chunk )
{
   page* that = *param;

   // n'what if kmem_param == pristine?
   // (n'what if *param == pristine?)
   if( chunk == 0 ) return ( void* )memory;
   int pid = 3;
   void* result = NULL;

   if( chunk > 0x1000 )
   {
      printf( "%08X byte allocation: ", chunk );
      // 0x578
      dword size = 0;
      page* pag = NULL;
      void* mem = kmem_param -> alloc( 0x578 );
      if( mem == NULL )
      {
         void* loc = pages_count_mm( chunk + 0x1000, &size );
         if( loc != NULL )
         {
            dword pg = ( ( dword )loc + ( dword )chunk );
            if( ( pg & 0x00000fff ) > 0 ) // asserting this.
            {
               pg &= 0xfffff000;
               pg += 0x00001000;
            }
            printf( "loc: %08X ", ( dword )loc - ( dword )memory );
            printf( "%08X\r\n", ( dword )loc - ( dword )memory + ( dword )chunk );
            pag = ( page* )pg;
            result = at_alloc_mm( loc, size, pid, pag );
         }
         else printf( "impossible\r\n" );
      }
      else
      {
         //printf( "%d bytes preallocated:\r\n", 0x578 );
         //kmem_param -> dump();
         kmem_param -> free( mem, 0x578 );
         //printf( "%d bytes freed:\r\n", 0x578 );
         //kmem_param -> dump();
         void* loc = pages_count_mm( chunk, &size );
         if( loc != NULL )
         {
            printf( "loc: %08X ", ( dword )loc - ( dword )memory );
            printf( "%08X\r\n", ( dword )loc - ( dword )memory + ( dword )chunk );
         //printf( "%08X size\r\n", size );
            result = at_alloc_mm( loc, size, pid, kmem_param );
         }
         else printf( "impossible\r\n" );
      }
   }
   else
   {
      result = leaf_alloc_mm( param, chunk, pid, &used );
   }
   // if out of ram, oom. (kill the largest resident)
   return result;   //NULL;//try1;
}

void fun( tabmm* leaf0, tabmm* twig0 )
{
   trunk = new_tabmm( 0 ); // override "new tabmm" when we get alloc_mm()
   tabmm* limb0 = new_tabmm( 0 );
   trunk -> m[ 0 ] = limb0; // 0x10000000
   tabmm* branch0 = new_tabmm( 0 );
   branch0 -> next = NULL; // 0016
   limb0 -> m[ 0 ] = branch0; // first 0x1000000
   //twig0
   branch0 -> m[ 0 ] = twig0; // first 0x100000 (1 MiB)
   //leaf0
   twig0 -> m[ 0 ] = leaf0; // first 16bit seg
   int sis = 0;
   tabmm* twig1 = new_tabmm( 1 << 20 ); // 0x100000
   twig0 -> next = twig1;
   twig1 -> prev = twig0;
   twig1 -> next = NULL; // 0016
   int boom = 0;
   tabmm* leaf1st = NULL;
   tabmm* prevleaf = NULL; // reserved.
   bool loaded = false;
   for( boom = 0; boom < 0x10; ++boom ) // allow kernel up to 2 MiB.
   {
      tabmm* leafn = new_tabmm( 0x100000 + ( boom << 0x10 ) );
      for( sis = 0; sis < 0x10; ++sis )
      {
         if( 0x100000 + ( sis << 12 ) + ( boom << 16 ) > KERN ) // NULL: unused.
         {
            loaded = true;
         }
         else
            leafn -> m[ sis ] = KMEM;
      }
      if( loaded && ( leaf1st == NULL ) ) leaf1st = leafn;
      if( prevleaf ) prevleaf -> next = leafn;
      leafn -> prev = prevleaf;
      prevleaf = leafn;
      twig1 -> m[ boom ] = leafn;
   }
   tabmm* prevtwig = twig1;
   branch0 -> m[ 1 ] = twig1;
   int bah = 2;
   for(; bah < 0x10; ++bah ) // climb to 16 MiB
   {
      tabmm* twign = new_tabmm( bah << 20 );
      for( boom = 0; boom < 0x10; ++boom )
      {
         twign -> m[ boom ] = NULL;//leafn;
      }
      if( bah == 3 ) continue; // no twig 3, used for rid table.
      if( prevtwig ) prevtwig -> next = twign;
      twign -> prev = prevtwig;
      prevtwig = twign;
      branch0 -> m[ bah ] = twign;
   }
   prevtwig -> next = NULL; // goes without saying
   branch0 -> m[ 3 ] = KMEM; // resident ident rid[][] table
   limb0 -> m[ 0 ] = branch0;
   tabmm* prevbranch = branch0;
   int faz = 1;
   for(; faz < 0x10; ++faz ) // scale to 256 MiB
   {
      tabmm* branchn = NULL;
      branchn = new_tabmm( faz << 24 );
      /*for( bah = 0; bah < 0x10; ++bah )
      {
         tabmm* twign = new_tabmm( ( faz << 24 ) + ( bah << 20 ) );
         for( boom = 0; boom < 0x10; ++boom )
         {
            tabmm* leafn = new_tabmm( ( faz << 24 ) + ( bah << 20 ) + ( boom << 0x10 ) );
            twign -> m[ boom ] = leafn;
         }
         branchn -> m[ bah ] = twign;
      }*/
      if( prevbranch ) prevbranch -> next = branchn;
      branchn -> prev = prevbranch;
      prevbranch = branchn;
      limb0 -> m[ faz ] = branchn; //NULL;
   }
   rids _rid( ( ridp* )( memory + 0x300000 ) );
   rid = _rid;
   rid[ 0 ][ 0 ] = limb0;
   rid[ 0 ][ 1 ] = branch0;
   rid[ 0 ][ 2 ] = twig1;
   rid[ 0 ][ 3 ] = leaf1st;

   page* here = ( page* )( memory + ( page_kludge_mm ) );
   //more( trunk, &here, 0 );
   int meh = 1;
   for(; meh < 5; ++meh ) // top it off at 640 MiB * 2.
   {
      tabmm* limbn = NULL;//new_tabmm( meh << 28 );
      /*   new_tabmm( ( meh << 28 ) + ( faz << 24 ) );
         for( bah = 0; bah < 0x10; ++bah )
         {
            tabmm* twign = new_tabmm( ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 ) );
            for( boom = 0; boom < 0x10; ++boom )
            {
               tabmm* leafn = new_tabmm( ( meh << 28 ) + ( faz << 24 ) + ( bah << 20 ) + ( boom << 16 ) );
               twign -> m[ boom ] = leafn;
            }
            branchn -> m[ bah ] = twign;
         }
         limbn -> m[ bah ] = branchn;
      }*/
      trunk -> m[ meh ] = limbn;
      if( meh == 2 ) break; // 640, fix-0014
   }
//printf( "meh: %d", meh );
   trunk -> m[ meh ] = new_tabmm( meh << 28 ); // a limb
      tabmm* limbo = ( tabmm* )( trunk -> m[ meh ] );
      for( faz = 0; faz < 0x10; ++faz )
      {
         tabmm* brancho = NULL;
         if( faz >= 0x8 )
         {
            limbo -> m[ faz ] = USED; // over the top
            continue;
         }
         brancho = new_tabmm( ( meh << 28 ) + ( faz << 24 ) );
         brancho -> next = NULL;
         if( prevbranch ) prevbranch -> next = brancho;
         brancho -> prev = prevbranch;
         prevbranch = brancho;
         limbo -> m[ faz ] = brancho;
      } // end fix-0014
   limbo -> prev = limb0;
   limb0 -> next = limbo; // 0017
   for( ++meh; meh < 0x10; ++meh ) // top it off at 4 GiB.
      trunk -> m[ meh ] = USED;
//   report_mm();
}

int shake()
{
   long int res = random();
   //printf( "%08X ", res );
   //return 128 * 1024 * 1024; // 1/8 GiB
   return res / 0x40;
}

int main3( int argc, char* argv[] )
{
   //byte* memory = new byte[ MEMORY ];
   memory = new byte[ MEMORY ]; // this part depends on your kernel and system.
   if( memory == NULL )
      return EXIT_FAILURE;
   bzero( memory, MEMORY );

   page* x = ( page* )( memory + START_KLUDGE_MM );
   x -> clear();

   bool ends = false;

   tabmm* leaf0 = new_tabmm( 0 );
   tabmm* twig0 = new_tabmm( 0 );

   int sis = 0;
   for(; sis < 0x10; ++sis )
      leaf0 -> m[ sis ] = USED;
   int boom = 1;
   for(; boom < 0xA; ++boom ) // conventional memory
   {
      tabmm* leafn = new_tabmm( boom << 0x10 );
      twig0 -> m[ boom ] = leafn;
      for( sis = 0; sis < 0x10; ++sis )
         leafn -> m[ sis ] = LOWM;
   }
   for(; boom < 0x10; ++boom ) // upper memory (vram, umb, and rom)
   {
      tabmm* leafn = new_tabmm( boom << 0x10 );
      twig0 -> m[ boom ] = leafn;
      for( sis = 0; sis < 0x10; ++sis )
         leafn -> m[ sis ] = USED;
   }

   fun( leaf0, twig0 );
   //printf( "%08X = pristine\r\n", ( dword )( page_kludge_mm + 4096 * 2 ) );
   pristine = ( page* )( memory + page_kludge_mm + 4096 * 2 );
   pristine -> clear();
   kmem_param = ( page* )( memory + page_kludge_mm );
   //kmem_param -> clear(); // fix-0016: this would overwrite memory substructure.

   page* here = NULL;//( page* )( memory + page_kludge_mm + 8192 ); // or NULL.
   int it = 0;
   // allocate 0x400 bytes each for 0x10000 iterations.
//   for(; it < 65535; ++it ) // here is the main test
//   for(; it < 80000; ++it ) // here is the main test
//   for(; it < 1460000; ++it ) // here is the main test fix-0013
   //if( false )
   //for(; it < 1470000; ++it ) // here is the stress test, > 640 MiB.
   for(; it < 200; ++it ) // here is the stress test, > 640 MiB.
   {
      dword rand = shake();
      dword sis = ( dword )more( &here, rand ); // 3640 // 400
      if( sis == ( dword )NULL )
      {
         printf( "%08X x\r\n", sis );
         break;
      }
      else // this could actually allocate 0x00000000
         printf( "%08X *\r\n", sis - ( dword )memory );
   }
   //new tabmm[ 65536 ][ 4 ];
   tabmm** me = &( rid[ 1 ][ 3 ] ); // KMEM is 2.
   tabmm** top = &( rid[ 0 ][ 0 ] );

   printf( "%08X = pristine\r\n", ( dword )( pristine ) - ( dword )memory );
   printf( "%08X = kmem_param\r\n", ( dword )( kmem_param ) - ( dword )memory );
   printf( "%08X <- memory\r\n", ( dword )memory );
   printf( "%08X <- sizeof( ridp )\r\n", ( dword )sizeof( ridp ) );
   printf( "%08X <- me\r\n", ( dword )me );
   printf( "%08X <- me - memory\r\n", ( dword )me - ( dword )memory );
   printf( "%08X <- top\r\n", ( dword )top - ( dword )memory );
   // this is likely to have climbed by 4096 once.
   printf( "%08X <- next unallocated kmem page\r\n", page_kludge_mm + 4096 + 8192 );

   delete memory;
   return EXIT_SUCCESS;
}

#include <stdlib.h>

extern int main3( int argc, char* argv[] );

int main( int argc, char* argv[] )
{
   main3( argc, argv );
   return EXIT_SUCCESS;
}
