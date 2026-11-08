/**
  ******************************************************************************
  * @file    printf_uart.c
  * @brief   自己实现的 printf（不依赖 newlib-nano 的格式化能力）
  *          - 输出口：USART2（PA2），与 PC 通信，115200-8-N-1
  *          - 支持的格式：%d %i %u %x %X %o %c %s %p %%
  *          - 支持宽度/标志：%-（左对齐）、%0（补零）、数字宽度，如 %04x、%8s
  *          - 支持长度：h / l / ll（h 按 int 取，l 按 long，ll 按 long long）
  *          - 不支持：%f / %e / %g 等浮点格式（嵌入式默认不链接浮点打印）
  *          数据先攒在 128 字节缓冲里，再一次性用 HAL 发送，速度比逐字节快
  ******************************************************************************
  */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "usart.h"

/* 输出缓冲区大小（栈只有 1KB，不要开太大） */
#define PRINTF_BUF_SIZE   128U

typedef struct
{
  char     buf[PRINTF_BUF_SIZE];
  uint16_t len;          /* 缓冲区中待发送的字节数 */
  int      cnt;          /* 累计已格式化的字符数（printf 返回值） */
} pbuf_t;

/* ==========================================================================
 * 缓冲区操作
 * ========================================================================== */

static void pbuf_flush(pbuf_t *pb)
{
  if (pb->len > 0U)
  {
    HAL_UART_Transmit(&huart2, (uint8_t *)pb->buf, pb->len, HAL_MAX_DELAY);
    pb->len = 0U;
  }
}

static void pbuf_putc(pbuf_t *pb, char c)
{
  if (pb->len >= PRINTF_BUF_SIZE)
  {
    pbuf_flush(pb);
  }

  pb->buf[pb->len] = c;
  pb->len++;
  pb->cnt++;
}

static void pbuf_puts(pbuf_t *pb, const char *s)
{
  while (*s != '\0')
  {
    pbuf_putc(pb, *s);
    s++;
  }
}

static void pbuf_pad(pbuf_t *pb, char c, int n)
{
  while (n > 0)
  {
    pbuf_putc(pb, c);
    n--;
  }
}

/* ==========================================================================
 * 数字格式化
 * ========================================================================== */

/**
 * @brief 输出无符号整数（十进制/八进制/十六进制）
 */
static void pbuf_put_uint(pbuf_t *pb, unsigned long long value, unsigned int base,
                          int upper, int width, int zero, int left)
{
  char tmp[24];
  int  n = 0;
  int  pad;
  const char *dig = (upper != 0) ? "0123456789ABCDEF" : "0123456789abcdef";
  unsigned long long b = (unsigned long long)base;

  if (value == 0ULL)
  {
    tmp[n] = '0';
    n++;
  }
  else
  {
    while (value != 0ULL)
    {
      tmp[n] = dig[(unsigned int)(value % b)];
      n++;
      value /= b;
    }
  }

  pad = width - n;

  if (left == 0)
  {
    pbuf_pad(pb, (zero != 0) ? '0' : ' ', pad);
  }

  while (n > 0)
  {
    n--;
    pbuf_putc(pb, tmp[n]);
  }

  if (left != 0)
  {
    pbuf_pad(pb, ' ', pad);
  }
}

/**
 * @brief 输出有符号整数（带负号、支持补零/左对齐）
 */
static void pbuf_put_int(pbuf_t *pb, long long value, int width, int zero, int left)
{
  char tmp[24];
  int  n = 0;
  int  pad;
  int  neg = (value < 0) ? 1 : 0;
  unsigned long long m;

  /* 取绝对值（写成 -(value+1)+1 是为了兼容 LLONG_MIN） */
  m = (neg != 0) ? ((unsigned long long)(-(value + 1)) + 1ULL)
                 : ((unsigned long long)value);

  if (m == 0ULL)
  {
    tmp[n] = '0';
    n++;
  }
  else
  {
    while (m != 0ULL)
    {
      tmp[n] = (char)('0' + (int)(m % 10ULL));
      n++;
      m /= 10ULL;
    }
  }

  pad = width - n - neg;

  if ((left == 0) && (zero == 0))
  {
    pbuf_pad(pb, ' ', pad);
  }

  if (neg != 0)
  {
    pbuf_putc(pb, '-');
  }

  if ((left == 0) && (zero != 0))
  {
    pbuf_pad(pb, '0', pad);
  }

  while (n > 0)
  {
    n--;
    pbuf_putc(pb, tmp[n]);
  }

  if (left != 0)
  {
    pbuf_pad(pb, ' ', pad);
  }
}

/* ==========================================================================
 * printf 本体
 * ========================================================================== */

int printf(const char *format, ...)
{
  va_list     ap;
  pbuf_t      pb;
  const char *p;
  int         ret;

  if (format == NULL)
  {
    return 0;
  }

  pb.len = 0U;
  pb.cnt = 0;

  va_start(ap, format);

  for (p = format; *p != '\0'; p++)
  {
    int left  = 0;      /* '-' 左对齐      */
    int zero  = 0;      /* '0' 补零        */
    int width = 0;      /* 最小宽度        */
    int longs = 0;      /* 长度修饰：0=int, 1=long, 2=long long */

    /* 普通字符直接输出 */
    if (*p != '%')
    {
      pbuf_putc(&pb, *p);
      continue;
    }

    p++;                /* 跳过 '%' */

    /* 标志位 */
    while ((*p == '-') || (*p == '0') || (*p == '+') || (*p == ' '))
    {
      if (*p == '-')
      {
        left = 1;
      }
      else if (*p == '0')
      {
        zero = 1;
      }

      p++;
    }

    /* 宽度 */
    while ((*p >= '0') && (*p <= '9'))
    {
      width = (width * 10) + (int)(*p - '0');
      p++;
    }

    /* 精度：本实现忽略，只跳过 */
    if (*p == '.')
    {
      p++;
      while ((*p >= '0') && (*p <= '9'))
      {
        p++;
      }
    }

    /* 长度修饰符 */
    while ((*p == 'l') || (*p == 'h') || (*p == 'z'))
    {
      if (*p == 'l')
      {
        longs++;
      }

      p++;
    }

    switch (*p)
    {
      case 'd':
      case 'i':
        if (longs >= 2)
        {
          pbuf_put_int(&pb, va_arg(ap, long long), width, zero, left);
        }
        else if (longs == 1)
        {
          pbuf_put_int(&pb, (long long)va_arg(ap, long), width, zero, left);
        }
        else
        {
          pbuf_put_int(&pb, (long long)va_arg(ap, int), width, zero, left);
        }
        break;

      case 'u':
        if (longs >= 2)
        {
          pbuf_put_uint(&pb, va_arg(ap, unsigned long long), 10U, 0, width, zero, left);
        }
        else if (longs == 1)
        {
          pbuf_put_uint(&pb, (unsigned long long)va_arg(ap, unsigned long), 10U, 0, width, zero, left);
        }
        else
        {
          pbuf_put_uint(&pb, (unsigned long long)va_arg(ap, unsigned int), 10U, 0, width, zero, left);
        }
        break;

      case 'x':
      case 'X':
        if (longs >= 2)
        {
          pbuf_put_uint(&pb, va_arg(ap, unsigned long long), 16U, (*p == 'X'), width, zero, left);
        }
        else if (longs == 1)
        {
          pbuf_put_uint(&pb, (unsigned long long)va_arg(ap, unsigned long), 16U, (*p == 'X'), width, zero, left);
        }
        else
        {
          pbuf_put_uint(&pb, (unsigned long long)va_arg(ap, unsigned int), 16U, (*p == 'X'), width, zero, left);
        }
        break;

      case 'o':
        pbuf_put_uint(&pb, (unsigned long long)va_arg(ap, unsigned int), 8U, 0, width, zero, left);
        break;

      case 'c':
        pbuf_putc(&pb, (char)va_arg(ap, int));
        break;

      case 's':
      {
        const char *s = va_arg(ap, const char *);
        int         len;
        int         pad;

        if (s == NULL)
        {
          s = "(null)";
        }

        len = (int)strlen(s);
        pad = width - len;

        if (left == 0)
        {
          pbuf_pad(&pb, ' ', pad);
        }

        pbuf_puts(&pb, s);

        if (left != 0)
        {
          pbuf_pad(&pb, ' ', pad);
        }
        break;
      }

      case 'p':
        pbuf_puts(&pb, "0x");
        pbuf_put_uint(&pb, (unsigned long long)(unsigned long)va_arg(ap, void *),
                      16U, 0, 0, 0, 0);
        break;

      case '%':
        pbuf_putc(&pb, '%');
        break;

      case '\0':        /* 格式串以 '%' 结尾，回退一格让外层循环结束 */
        p--;
        break;

      default:          /* 不认识的格式符：原样输出，方便排查 */
        pbuf_putc(&pb, '%');
        pbuf_putc(&pb, *p);
        break;
    }
  }

  va_end(ap);

  pbuf_flush(&pb);

  ret = pb.cnt;

  return ret;
}
