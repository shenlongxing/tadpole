# tadpole
tadpole是一个简单的有序kv系统，支持put/get/delete/scan接口，实现了简单的持久化功能，可以做到重启数据不丢失。
在实现上，参考了Redis的一些设计。使用skiplist+hash table来保存有序的数据，通信协议使用的是resp(REdis Serialization Protocol)，使用Redis client进行通信。

## 编译
**目前只支持linux平台，并在Makefile中做了限制**

    $ cd tadpole
    $ make

## 配置
支持的配置选项如下：
    
    loglevel notice         # 日志级别
    dir /tmp                # 数据目录，持久化的数据文件/日志文件存放的位置
    logfile "tadpole.log"   # log文件，生成在dir目录下
    daemonize yes           # daemon化
    port 6666               # 监听端口
    fixed-length 8 16       # key/value是否配置为固定长度
    dbfilename tadpole.data # 持久化的数据文件名，生成在dir目录下

其中，key/val可以使用定长，也可以不定长度。通过fixed-length选项进行配置，默认key长度为16字节，value 256字节。不配置则表示kv长度不限。

## 运行

    $ ./tadpole -c tadpole.conf
    
## 使用
由于使用的是Redis的通信协议，直接使用Redis客户端进行操作。
### put
使用put命令插入单个key value对

    $ redis-cli -p 6666 put key:0001 value:0000000001
    
### get
使用get命令获取key对应的value值

    $ redis-cli -p 6666 get key:0001
    
### delete
使用delete命令删除一个key value对

    $ redis-cli -p 6666 get key:0001
    
### scan
scan命令返回两个key范围的全部key，**输入时，需要保证后面的key大于等于前面的key**

    $ redis-cli -p 6666 scan key:0001 key:9999

### show
show命令显示当前系统的状态，包括：kv总数，最小key，最大key

    $ redis-cli -p 6666 show

## 持久化
tadpole目前只支持简单的持久化功能，在系统退出时将内存中的key/value对写入到数据文件中。重启时加载数据文件，load重启前的数据到内存中。
后续会支持手动触发，以及定时触发的持久化功能。

## test
使用Redis-cli进行put/get/delete/scan/info各个接口的测试，只需如下命令：

    $ make test
    
**由于使用了redis-cli，请提前安装好redis客户端，并将PATH添加到.~/.bashrc中**

## 性能
使用Redis的benchmark进行了put/get的基准测试，2C4G的配置，256字节的value长度，100个客户端，QPS可以实现5w以上。后续补充不同长度的基准测试。
