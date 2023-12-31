// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int);

static int panicked = 0;

static struct
{
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if (sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do
  {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (--i >= 0)
    consputc(buf[i]);
}
// PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if (locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint *)(void *)(&fmt + 1);
  for (i = 0; (c = fmt[i] & 0xff) != 0; i++)
  {
    if (c != '%')
    {
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if (c == 0)
      break;
    switch (c)
    {
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if ((s = (char *)*argp++) == 0)
        s = "(null)";
      for (; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if (locking)
    release(&cons.lock);
}

void panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for (i = 0; i < 10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for (;;)
    ;
}

// PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort *)P2V(0xb8000); // CGA memory

static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  if (c == '\n')
    pos += 80 - pos % 80;
  else if (c == BACKSPACE)
  {
    if (pos > 0)
      --pos;
  }
  else
    crt[pos++] = (c & 0xff) | 0x0700; // black on white

  if (pos < 0 || pos > 25 * 80)
    panic("pos under/overflow");

  if ((pos / 80) >= 24)
  { // Scroll up.
    memmove(crt, crt + 80, sizeof(crt[0]) * 23 * 80);
    pos -= 80;
    memset(crt + pos, 0, sizeof(crt[0]) * (24 * 80 - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
  crt[pos] = ' ' | 0x0700;
}

void consputc(int c)
{
  if (panicked)
  {
    cli();
    for (;;)
      ;
  }

  if (c == BACKSPACE)
  {
    uartputc('\b');
    uartputc(' ');
    uartputc('\b');
  }
  else
    uartputc(c);
  cgaputc(c);
}

#define INPUT_BUF 128
struct
{
  char buf[INPUT_BUF];
  char line_ahead[INPUT_BUF];
  uint line_ahead_size;
  uint r; // Read index
  uint w; // Write index
  uint e; // Edit index
} input;

#define C(x) ((x) - '@') // Control-x

void cursor_move_left(int length)
{
  for (uint i = 0; i < length; i++)
  {
    int pos;
    outb(CRTPORT, 14);
    pos = inb(CRTPORT + 1) << 8;
    outb(CRTPORT, 15);
    pos |= inb(CRTPORT + 1);

    if (pos > 0)
      --pos;

    outb(CRTPORT, 14);
    outb(CRTPORT + 1, pos >> 8);
    outb(CRTPORT, 15);
    outb(CRTPORT + 1, pos);

    uartputc('\b');
  }
}

void print_cursor_right_hand(int is_backspace) // print the characters that are place on the right of the cursor
{
  if (is_backspace)
  {
    for (int i = 0; i <= input.line_ahead_size + 1; i++)
      uartputc(' ');

    for (int i = 0; i <= input.line_ahead_size + 1; i++)
      uartputc('\b');
  }

  for (int i = input.line_ahead_size - 1; i >= 0; i--)
  {
    consputc(input.line_ahead[i]);
  }

  cursor_move_left(input.line_ahead_size);
}

// ARROW UP AND DOWN:
#define COMMAND_MEMORY_LENGHT 15
#define MAX_COMMAND_LENGTH 128

char cmdAry[COMMAND_MEMORY_LENGHT][MAX_COMMAND_LENGTH] = {""};
uint cmdAryPtr = 0;
uint arrowUpIndex = 0;
uint arrowDownIndex = -1;
//------------------------------------------

void consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;
  uint charPtr;
  acquire(&cons.lock);
  while ((c = getc()) >= 0)
  {
    switch (c)
    {
    case C('P'): // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      doprocdump = 1;
      break;
    case C('U'): // Kill line.
      while (input.e != input.w &&
             input.buf[(input.e - 1) % INPUT_BUF] != '\n')
      {
        input.e--;
        consputc(BACKSPACE);
      }
      break;
    case C('B'):
      if (input.e != input.w)
      {
        input.e--;
        char last_char = input.buf[(input.e) % INPUT_BUF];
        input.line_ahead[input.line_ahead_size++] = last_char;
        cursor_move_left(1);

        print_cursor_right_hand(0);
      }
      break;
    case C('F'):
      if (input.e != input.w && input.line_ahead_size > 0)
      {
        input.e++;
        consputc(input.line_ahead[--input.line_ahead_size]);
      }
      break;
    case C('L'):
      for (int i = 0; i < 25 * 80; i++)
        crt[i] = ' ' | 0x0700; // Clear the screen

      for (int i = 0; i < 25 * 80; i++) // the size is difference to xv6
      {
        uartputc('\b'); // Clear one line of terminal
        uartputc(' ');
        uartputc('\b');
      }
      uartputc('$');

      // Repositioning the cursor to the top left corner:
      outb(CRTPORT, 14);
      outb(CRTPORT + 1, 0);
      outb(CRTPORT, 15);
      outb(CRTPORT + 1, 1);
      input.e = input.w = input.r; // Reset input buffer indices
      input.line_ahead_size = 0;
      // Print a dollar sign:
      crt[0] = '$' | 0x0700;
      break;

    case C('H'):
    case '\x7f': // Backspace
      if (input.e != input.w)
      {
        input.e--;
        consputc(BACKSPACE);
        print_cursor_right_hand(1);
      }
      break;

    // Ctrl+M instead of arrow up
    case C('M'):
      if (arrowUpIndex > 0)
      {
        while (input.e != input.w && input.buf[--input.e % INPUT_BUF] != '\n')
        {
          consputc(BACKSPACE);
        }
        for (uint i = 0; (i < MAX_COMMAND_LENGTH) && cmdAry[arrowUpIndex][i] != '\n'; i++)
        {
          input.buf[input.e++ % INPUT_BUF] = cmdAry[arrowUpIndex][i];
          consputc(cmdAry[arrowUpIndex][i]);
        }
        arrowUpIndex = (arrowUpIndex - 1) % COMMAND_MEMORY_LENGHT;
        arrowDownIndex = arrowUpIndex + 1;
      }

      break;
    case C('N'):
      if (arrowDownIndex != -1 && arrowDownIndex < cmdAryPtr)
      {
        while (input.e != input.w && input.buf[--input.e % INPUT_BUF] != '\n')
        {
          consputc(BACKSPACE);
        }
        // Handle Ctrl+N to retrieve next commands.
        arrowDownIndex = (arrowDownIndex + 1) % COMMAND_MEMORY_LENGHT;
        for (uint i = 0; (i < MAX_COMMAND_LENGTH) && cmdAry[arrowDownIndex][i] != '\n'; i++)
        {
          input.buf[input.e++ % INPUT_BUF] = cmdAry[arrowDownIndex][i];
          consputc(cmdAry[arrowDownIndex][i]);
        }
        arrowUpIndex = arrowDownIndex - 1;
      }
      break;
    default:
      if (c != 0 && input.e - input.r < INPUT_BUF)
      {

        // Save each command into an array:
        if (c == '\n')
        {
          charPtr = input.e;
          while (charPtr != input.w && input.buf[--charPtr % INPUT_BUF] != '$')
            ; // find first charachter in a command(charPtr)

          cmdAryPtr = (cmdAryPtr + 1) % COMMAND_MEMORY_LENGHT;
          for (uint i = 0; (i < MAX_COMMAND_LENGTH - 1) && ((charPtr + i) < input.e); i++)
          {
            cmdAry[cmdAryPtr][i] = input.buf[(charPtr + i) % INPUT_BUF];
            cmdAry[cmdAryPtr][i + 1] = '\n';
          }
          arrowUpIndex = cmdAryPtr;
        }
        //-----------------------------------------------

        c = (c == '\r') ? '\n' : c;
        input.buf[input.e++ % INPUT_BUF] = c;
        consputc(c);
        print_cursor_right_hand(0);

        if (c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF)
        {
          input.w = input.e;
          input.line_ahead_size = 0;
          wakeup(&input.r);
        }
      }
      break;
    }
  }
  release(&cons.lock);
  if (doprocdump)
  {
    procdump(); // now call procdump() wo. cons.lock held
  }
}

int consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while (n > 0)
  {
    while (input.r == input.w)
    {
      if (myproc()->killed)
      {
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if (c == C('D'))
    { // EOF
      if (n < target)
      {
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if (c == '\n')
      break;
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for (i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}
