HTTP Parser
===========

HTTP Parser是用C实现的HTTP消息解析器, 可处理请求和响应. 它用于高性能HTTP应用程序,
没有调用任何系统函数或进行内存分配. 它并不缓存数据, 可在任何时刻中断处理. 对于
每个消息流(在Web Server中也就是一个连接)它仅需要大约40字节数据, 这与所在系统相关.

特性:

- 没有任何第3方依赖
- 处理长连接(keep-alive)
- 解码chunked编码
- 支持upgrade
- 抵御缓冲区举出攻击

可解出以下信息:

- 首部字段和值
- Content-Length
- Request method
- Response status code
- Transfer-Encoding
- HTTP version
- Request URL
- Message body


用法
-----

每个TCP连接需要一个`http_parser`对象, 它通过`http_parse_init()`初始化, 且可以设置回调函数.
下面代码初始化一个request parser:

```c
http_parser_settings settings;
settings.on_url = my_url_callback;
settings.on_header_field = my_header_field_callback;
/* ... */

http_parser *parser = malloc(sizeof(http_parser));
http_parser_init(parser, HTTP_REQUEST);
parser->data = my_socket;
```

当从socket接收到数据后, 调用`http_parser_execute`执行解析并检查错误:

```c
size_t len = 80*1024, nparsed;
char buf[len];
ssize_t recved;

recved = recv(fd, buf, len, 0);

if (recved < 0) {
  /* 处理错误. */
}

/* 开始/继续进行解析.
 * 注意传入recved==0表示已接收到EOF.
 */
nparsed = http_parser_execute(parser, &settings, buf, recved);

if (parser->upgrade) {
  /* 处理新协议 */
} else if (nparsed != recved) {
  /* 处理错误, 通常要关闭连接. */
}
```

Http Parser需要知道报文流在何片结束. 例如, 有时server发送不带Content-Length的响应,
让client消费输入数据(body)直到EOF. `http_parse_execute()`的第4个参数传入`0`即可
通知Http Parser已到EOF. 在遇到EOF期间, 回调函数或错误仍可能发生, 因此仍必须处理它们.

标量信息, 如`statsu_code`, `method`, 以及HTTP version储存在parser结构体中. 这些
数据只是临时存储在那里, 在新的消息到来时会被reset. 如果后续需要这些信息, 需要在
`header_complete`回调函数中对其进行拷贝.

Http Parser对request和response中的transfer-encoding的解码是透明的, 因此在送到
`on_body`回调函数之前, chunked编码已经被解码了.


upgrade的特殊情况
------------------

Http Parser支持连接upgrade为不同协议. 一个越来越常见的例子是WebSocket协议, 它会
在非HTTP数据之后发送类似下面的请求:

    GET /demo HTTP/1.1
    Upgrade: WebSocket
    Connection: Upgrade
    Host: example.com
    Origin: http://example.com
    WebSocket-Protocol: sample


(关于WebSocket协议可参考 [RFC6455](https://tools.ietf.org/html/rfc6455). )

对于这种情况, HTTP Parser会将其视为没有body的普通HTTP消息, 并发射`on_header_complete`
和`on_message_complete`事件. 不过`http_parser_execute()`会在首部之后停止解析并返回.


回调函数
----------

在`http_parser_execute()`调用期间, `http_parser_settings`中设置的回调函数会被执行.
HTTP Parser会保存状态(state)且不回看, 因此不需要缓存数据. 如果你需要保存特定数据
以备后续使用, 可以在回调函数中去做.

回调函数有两种类型:

- 通知 `typedef int (*http_cb) (http_parser*);`
  
  包括: `on_message_begin`, `on_headers_complete`, `on_message_complete`

- 数据 `typedef int (*http_data_cb) (http_parser*, const char *at, size_t length);`

  包括: `on_url`(仅request), `on_header_field`, `on_header_value`, `on_body`.

回调函数应在成功时返回`0`, 返回非0值表示错误, 会导致parser立即退出.

当需要通过回调函数传入或传出本地信息时, 可以使用`http_parser`对象的`data`字段.
例如使用多线程来处理socket连接, 解析request, 然后给出响应的时候. 把已接受的socket,
写缓冲区等参数做为线程局部对象做为`data`传入/传出, 回调函数就可以在线程范围和
回调函数范围之间以线程安全地方式进行通信.

示例:

```c
 typedef struct {
  socket_t sock;
  void* buffer;
  int buf_len;
 } custom_data_t;


int my_url_callback(http_parser* parser, const char *at, size_t length) {
  /* 访问线程线程局部的自定义数据. */
  parser->data;
  ...
  return 0;
}

...

void http_parser_thread(socket_t sock) {
 int nparsed = 0;
 /* 分配用户自定义数据 */
 custom_data_t *my_data = malloc(sizeof(custom_data_t));
/* 存放回调函数要用的信息, 将由线程传给回调函数 */
 my_data->sock = sock;

 /* 实例化线程局部的parser */
 http_parser *parser = malloc(sizeof(http_parser));
 http_parser_init(parser, HTTP_REQUEST);
 /* 自定义数据是通过parser的data成员来访问的, parser指针会被传入回调函数 */
 parser->data = my_data;

 http_parser_settings settings;
 settings.on_url = my_url_callback;

 nparsed = http_parser_execute(parser, &settings, buf, recved);

 ...
 /* 回调函数中, 可将解析后的信息写入线程局部的buffer. 它将由回调函数传给线程 */
 my_data->buffer;
 ...
}

```

当分块解析HTTP消息时, 数据回调函数会被调用多次. HTTP Parser仅保证`data`指针在回调\
函数生命周期内有效. 如果需要, 你可以把数据`read()`到堆上的buffer以避免多次拷贝内存.

如果部分地读取/解析首部是有些技巧性的. 基本上, 你需要记录上一次首部回调函数是
字段还是值, 并参考下表的逻辑:

    (on_header_field and on_header_value shortened to on_h_*)
     ------------------------ ------------ --------------------------------------------
    | State (prev. callback) | Callback   | Description/action                         |
     ------------------------ ------------ --------------------------------------------
    | nothing (first call)   | on_h_field | Allocate new buffer and copy callback data |
    |                        |            | into it                                    |
     ------------------------ ------------ --------------------------------------------
    | value                  | on_h_field | New header started.                        |
    |                        |            | Copy current name,value buffers to headers |
    |                        |            | list and allocate new buffer for new name  |
     ------------------------ ------------ --------------------------------------------
    | field                  | on_h_field | Previous name continues. Reallocate name   |
    |                        |            | buffer and append callback data to it      |
     ------------------------ ------------ --------------------------------------------
    | field                  | on_h_value | Value for current header started. Allocate |
    |                        |            | new buffer and copy callback data to it    |
     ------------------------ ------------ --------------------------------------------
    | value                  | on_h_value | Value continues. Reallocate value buffer   |
    |                        |            | and append callback data to it             |
     ------------------------ ------------ --------------------------------------------


解析URL
----------

HTTP Parser还提供了一个很简单的, 零拷贝的URL解析函数`http_parser_parse_url()`,
它可以帮助你在`on_url`回调函数中解析URL.


示例
------

`contrib`目录有两个示例:

- parsertrace.c 解析包含HTTP消息的文件, 并输出解析出的信息. 用法:
  
      $./parsertrace_g 
      Usage: ./parsertrace_g $type $filename
        type: -x, where x is one of {r,b,q}
        parses file as a Response, reQuest, or Both

- url_parser.c 解析命令行参数给出的URL, 并输出解析结果. 用法:

      $./url_parser connect|get url