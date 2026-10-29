# -*- coding: utf-8 -*-
"""实验一的五节正文内容。每节返回一段 OOXML 字符串（插入模板正文表格对应单元格）。"""
from docx_kit import P, H, CODE, LI, NOTE, TABLE, SPACER


# ============================================================ 一、试验任务
def sec_task():
    o = []
    o.append(P('实验一对应《数据库系统》实验指导书"二、实验任务"中的第 1 项与第 2 项。'
               '指导书共列出 8 项任务、分 4 次实验完成、每次 2 项，实验一承担的是整个系列中最基础的两项，'
               '既要求把开发环境与工具链跑通，也要求第一次真正改动 MiniOB 的源码。'))
    o.append(H('1.1 任务内容'))
    o.append(LI('任务 1：验证 SELECT 语句的正确性。这里的"正确性"特指 SELECT 语句中出现的表名与字段名'
                '是否真实存在——要求 MiniOB 对不存在的表名、不存在的字段名都能给出明确的错误响应，'
                '既不返回错误的数据，也不能崩溃。该项任务是验证性任务，不改代码，但要给出可复现的测试记录'
                '并从源码层面说明校验发生在哪一层。'))
    o.append(LI('任务 2：实现 drop table 功能。MiniOB 原始代码中该语句只写了语法规则，'
                '语义转换与执行两条链路上都是空的，执行时直接返回 UNIMPLEMENTED。'
                '需要补齐从语法树到存储层的完整实现，使 drop table 能同时删除表在内存中的元数据对象'
                '与磁盘上的全部物理文件，并保证删除后可以重建同名表、重启后表不会"复活"。'))
    o.append(H('1.2 实验环境'))
    o.append(TABLE([
        ['项目', '配置'],
        ['宿主操作系统', 'Windows 11 24H2（build 26100）'],
        ['虚拟化 / 容器', 'WSL2（内核 6.18）+ Docker Desktop 4.95.0，引擎 29.8.2'],
        ['容器镜像', 'oceanbase/miniob（基于 Ubuntu 24.04）'],
        ['开发方式', 'VS Code + Dev Containers 扩展，源码目录挂载到容器内 /workspaces/miniob'],
        ['编译工具链', 'g++（C++20）、cmake、make、flex、bison'],
        ['调试工具', 'gdb、AddressSanitizer（CMake 中 ENABLE_ASAN 默认开启）'],
        ['源码版本', 'oceanbase/miniob，commit 3259c37，5 个第三方子模块已初始化'],
    ], [2200, 6100]))
    o.append(H('1.3 验收依据'))
    o.append(P('本次实验的功能验收依据仓库自带的测试用例 test/case/test/basic.test 与 '
               'test/case/test/primary-drop-table.test。按指导书要求，MiniOB 的运行结果应与 MySQL 8.0 一致；'
               '由于仓库中绝大部分用例已不再附带 .result 期望文件，'
               '这部分用例需以 --report-only 方式运行并人工比对 MySQL 8.0 的输出。'))
    return ''.join(o)


# ============================================================ 二、试验准备情况
def sec_prepare():
    o = []
    o.append(P('实验前的准备分两条线：一条是把环境与工具链跑通，另一条是先把 MiniOB 的执行机理读懂，'
               '使两项任务在动手之前就已经有明确的实现路径和风险预判。'))

    o.append(H('2.1 架构预习'))
    o.append(P('MiniOB 是一条典型的单机数据库分层结构，一条 SQL 从进入服务端到返回结果要依次经过以下各层。'
               '把这张分层图记住之后，任何一条新语句要改哪里、要改几个文件，都可以直接定位。'))
    o.append(TABLE([
        ['层次', '目录 / 文件', '职责'],
        ['网络层', 'net/', '监听连接；plain / mysql / cli 三种协议的通讯器'],
        ['事件层', 'event/', '把请求封装成事件，交给线程池排队'],
        ['会话层', 'session/session_stage.cpp', '总调度：cache → parse → resolve → optimize → execute'],
        ['语法层', 'sql/parser/yacc_sql.y、lex_sql.l', 'flex/bison 生成词法与语法分析，产出 ParsedSqlNode'],
        ['语义层', 'sql/stmt/stmt.cpp', '把 ParsedSqlNode 转换成 Stmt，并做依赖当前数据库状态的校验'],
        ['优化层', 'sql/optimizer/', '逻辑计划、规则重写（谓词下推等）、物理计划生成'],
        ['执行层', 'sql/operator/、sql/executor/', 'DML 走算子树的迭代执行；DDL 走命令执行器'],
        ['存储层', 'storage/', 'record / table / index / buffer / clog / trx / db'],
    ], [1250, 3050, 4000]))

    o.append(H('2.2 三个直接影响本次实现的关键结论'))
    o.append(P('这三个结论是在读源码（而不是读文档）的过程中确认的，后面两项任务的方案都建立在它们之上。'))
    o.append(LI('结论一：数据目录是"当前工作目录下的 miniob"，而不是固定绝对路径。'
                'src/observer/common/init.cpp 第 143 行写的是字面量 GCTX.handler_->init("miniob", ...)，'
                'DefaultHandler::init 再在其下建 db/sys。因此在哪个目录启动 observer，'
                '数据就落在哪个目录的 miniob/db/sys 下；换一个目录启动就是一套全新的空库。'
                '这条结论决定了后续所有测试都必须固定在同一目录下进行。'))
    o.append(LI('结论二：DDL 与 DML 走两条完全不同的通道，分岔点在 execute_stage.cpp。'
                '该文件用"物理算子是否为空"作为判据：physical_operator != nullptr 时走算子迭代执行，'
                '否则交给 CommandExecutor。而物理算子只有 OptimizeStage 才会设置，'
                '逻辑计划生成器（logical_plan_generator.cpp）的 switch 中只有 CALC/SELECT/INSERT/DELETE/EXPLAIN 五个分支，'
                '其余语句一律返回 UNIMPLEMENTED，于是 create table、drop table 这类 DDL 自然掉进命令执行器通道。'
                '因此本次任务 2 完全不需要写 sql/operator/ 下的算子，也不需要改优化器。'))
    o.append(LI('结论三：表的物理存在形式就是"元数据文件名"。MiniOB 没有系统表，'
                'Db::open_all_tables() 在启动时靠扫描 *.table 文件把上一运行期的表全部加载回来。'
                '由此可以推断：drop table 如果只删除内存对象而不删除 .table 文件，'
                '则重启后表会重新出现，并且同名表再也无法创建（Table::create 使用 O_CREAT|O_EXCL）。'
                '这一点成为任务 2 方案设计的核心约束。'))

    o.append(H('2.3 任务 1 的预案'))
    o.append(P('任务 1 是验证性任务，预案是设计四组用例，覆盖"表名错误""字段名错误""两者都正确"三种情形，'
               '并预先从源码中查出对应的错误码，以便判断输出是否正确。预判的错误码为：'
               '表名不存在对应 RC::SCHEMA_TABLE_NOT_EXIST，字段名不存在对应 RC::SCHEMA_FIELD_MISSING。'))
    o.append(TABLE([
        ['用例', 'SQL 语句', '预期结果'],
        ['A 表名正确 + 字段名正确', 'select id, name from student;', '返回结果集'],
        ['B 表名不存在', 'select * from no_such_table;', '失败，表不存在'],
        ['C 表名正确 + 字段名不存在', 'select no_such_field from student;', '失败，字段不存在'],
        ['D 合法表但结果为空', 'select * from score where score > 1000;', '返回空结果集，不报错'],
    ], [2400, 3400, 2500]))
    o.append(NOTE('用例 D 的意义：区分"查不到数据"与"查不到对象"。前者是正常的空结果集，'
                  '后者才是错误。二者在客户端上表现不同，是判断校验是否正确的关键对照。'))

    o.append(H('2.4 任务 2 的预案'))
    o.append(P('按"参照 create table 实现 drop table"的思路（指导书也给出过同样建议），'
               '先把 create table 的完整链路逐层读通，再按镜像结构反推出需要补齐的位置。'
               'create table 涉及 5 个文件，drop table 只需补齐其中的语义与执行两段，'
               '再加一处存储层方法。预案中的改动清单如下。'))
    o.append(TABLE([
        ['序号', '文件', '改动内容'],
        ['1', 'sql/stmt/drop_table_stmt.h', '新建 DropTableStmt 类'],
        ['2', 'sql/stmt/drop_table_stmt.cpp', '新建 create()，校验表存在'],
        ['3', 'sql/executor/drop_table_executor.h', '新建 DropTableExecutor 类'],
        ['4', 'sql/executor/drop_table_executor.cpp', '新建 execute()，转交 Db 层'],
        ['5', 'sql/stmt/stmt.cpp', '增加 include 与 case SCF_DROP_TABLE'],
        ['6', 'sql/executor/command_executor.cpp', '增加 include 与 case StmtType::DROP_TABLE'],
        ['7', 'storage/db/db.h', '声明 Db::drop_table'],
        ['8', 'storage/db/db.cpp', '实现 Db::drop_table（摘哈希表、析构、删文件）'],
    ], [700, 3300, 4300]))
    o.append(P('预案中同时预判了两个必须绕开的坑：'))
    o.append(LI('坑一：只删内存不删文件。Db 用哈希表 opened_tables_ 记录"有哪些表"，'
                '从哈希表里摘掉后 show tables 确实看不到了，但磁盘文件仍在，'
                '结果是重启后表复活、且重建同名表报 SCHEMA_TABLE_EXIST。'
                '验收用例 primary-drop-table.test 的第 3、5 组专门测这一点。'))
    o.append(LI('坑二：析构顺序引起的 use-after-free。必须先从哈希表 erase 再 delete 对象，'
                '删除之后绝不能再用 table->name() 之类的成员。项目默认开启 AddressSanitizer，'
                '这类错误会被当场抓住；db.cpp 中也留有一条历史注释，说明前一版正是踩了这个坑。'))

    o.append(H('2.5 风险与对策'))
    o.append(TABLE([
        ['预判风险', '对策'],
        ['容器访问不到 github.com，build.sh init 会静默失败', '不执行 build.sh init；官方镜像已把 5 个第三方库预装在 /usr/local'],
        ['CMake 开启 -Wall -Werror，警告即错误', '代码中不留未使用变量，注意初始化列表顺序'],
        ['首次全量编译极慢（源码位于 Windows 挂载盘）', '只做增量编译；改代码尽量不动被广泛 include 的头文件'],
        ['默认 vacuous 事务模型不保证持久化', '启动时加 -d disk 参数'],
        ['切换工作目录导致数据目录错位', '所有测试固定在同一个目录下启动 observer'],
    ], [3600, 4700]))
    return ''.join(o)


# ============================================================ 三、试验过程记录
def sec_process():
    o = []
    o.append(H('3.1 任务 1：验证 SELECT 语句的表名与字段名'))
    o.append(P('先把"校验发生在哪一层"查清楚，再用用例去验证，这样即使结果异常也能定位原因，'
               '而不是靠猜。'))

    o.append(H('3.1.1 校验逻辑的定位'))
    o.append(P('阅读源码后确认，SELECT 的表名与字段名校验都发生在优化器之前的语义阶段，'
               '两者由不同的代码位置负责，返回的错误码也不同。'))
    o.append(LI('表名校验：sql/stmt/select_stmt.cpp 第 53—57 行。'
                'SelectStmt::create 遍历 FROM 子句中的每个表名，调用 db->find_table()，'
                '返回空指针时立即 return RC::SCHEMA_TABLE_NOT_EXIST。'
                '也就是说表名校验在 Stmt 层就完成了，连逻辑计划都不会生成。'))
    o.append(LI('字段名校验：sql/parser/expression_binder.cpp 第 164—168 行。'
                'ExpressionBinder 拿到表对象后调用 table->table_meta().field(field_name)，'
                '返回空指针时 return RC::SCHEMA_FIELD_MISSING。'
                '注意它只接受"表名.字段名"或"字段名"两种写法，字段名为 "*" 时走通配符展开分支。'))
    o.append(LI('错误如何呈现：net/plain_communicator.cpp 第 102 行，'
                'return_code 为 SUCCESS 时输出 "SUCCESS"，否则输出 "FAILURE"。'
                'cli 协议（-P cli）的 CliCommunicator 直接继承自 PlainCommunicator，因此输出格式相同。'
                '所以任何语义失败在命令行上表现为 FAILURE，具体错误码要去 observer.log 里看。'))

    o.append(H('3.1.2 用例执行与结果'))
    o.append(P('在固定目录下启动 observer，先建好对照用的表并插入少量数据，然后逐条执行四组用例，'
               '并同时观察客户端输出与日志中的错误码。'))
    o.append(CODE([
        'cd /root',
        'observer -f /workspaces/miniob/etc/observer.ini -P cli -d disk',
        '',
        'miniob > create table student(id int, name char(20), age int, class_id int);',
        'miniob > insert into student values(1, \'xiaoming\', 18, 1);',
        'miniob > create table score(student_id int, course_id int, score int);',
        'miniob > insert into score values(1, 1, 95);',
    ]))
    o.append(TABLE([
        ['用例', 'SQL', '客户端输出', '日志中的错误码', '结论'],
        ['A', 'select id, name from student;', '结果集（1 行）', '无', '正常'],
        ['B', 'select * from no_such_table;', 'FAILURE', 'SCHEMA_TABLE_NOT_EXIST', '正确报错'],
        ['C', 'select no_such_field from student;', 'FAILURE', 'SCHEMA_FIELD_MISSING', '正确报错'],
        ['D', 'select * from score where score > 1000;', '结果集（0 行）', '无', '正常'],
    ], [500, 2750, 1500, 2150, 1100]))

    o.append(H('3.1.3 结果分析'))
    o.append(P('四组用例全部符合预期，说明 MiniOB 对 SELECT 语句的对象名校验是完备的，'
               '并且校验时机足够早——都在 Stmt / resolve 阶段，不会进入优化与执行。'))
    o.append(P('对比用例 A 与用例 B/C 可以反过来确认一个重要的架构事实：'
               '语义错误和"查不到数据"是两种完全不同的结果，前者是 FAILURE，后者是带表头的空结果集。'
               '这一点在后面任务 2 的验证中同样要用到——判断表是否真的被删除，'
               '要看 show tables 的结果，而不是看 select 是否报错，'
               '因为 select 一张已被删除的表同样只会返回 FAILURE，与"表从来不存在"无法区分。'))
    o.append(NOTE('任务 1 的验证过程还暴露出一个与实现无关、但会影响实验效率的问题：'
                  '数据目录跟着当前工作目录走，切换目录启动 observer 会得到一个全新的空库，'
                  '表现为任何 select 都返回 FAILURE。排查方法是先执行 show tables 看当前库里有没有表，'
                  '再回头确认启动目录。'))

    o.append(H('3.2 任务 2：实现 drop table'))
    o.append(H('3.2.1 断点定位'))
    o.append(P('逐层追踪 drop table 的执行路径，发现语法层其实是完整的，'
               '断点集中在语义层与执行层，另外存储层的方法体也是空的。'))
    o.append(TABLE([
        ['层次', '位置', '原始状态'],
        ['语法层', 'yacc_sql.y 第 279—283 行', '完整，已产出 SCF_DROP_TABLE 并填 relation_name'],
        ['数据结构', 'parse_defs.h 第 170 行', '完整，DropTableSqlNode 已定义'],
        ['枚举', 'stmt/stmt.h 第 39 行', '完整，StmtType::DROP_TABLE 已定义'],
        ['语义分发', 'stmt/stmt.cpp', '缺失，switch 中无 case SCF_DROP_TABLE，落 default 返回 UNIMPLEMENTED'],
        ['语义转换', 'sql/stmt/drop_table_stmt.*', '整个类不存在'],
        ['执行分发', 'command_executor.cpp', '缺失，switch 中无 case StmtType::DROP_TABLE'],
        ['执行器', 'sql/executor/drop_table_executor.*', '整个类不存在'],
        ['存储层', 'default_handler.cpp 第 145 行', '函数体直接 return RC::UNIMPLEMENTED'],
        ['存储层', 'storage/db/db.*', 'Db 类根本没有 drop_table 方法'],
    ], [1500, 3000, 3800]))

    o.append(H('3.2.2 实现方案'))
    o.append(P('按预案补齐 8 个文件（4 个新建、4 个修改），结构上与 create table 严格对称。'
               '由于 src/observer/CMakeLists.txt 使用 FILE(GLOB_RECURSE *.cpp) 收集源文件，'
               '新增的 .cpp 无需修改任何构建脚本即可被编译。'))
    o.append(TABLE([
        ['文件', '作用'],
        ['sql/stmt/drop_table_stmt.h', '定义 DropTableStmt，type() 返回 StmtType::DROP_TABLE'],
        ['sql/stmt/drop_table_stmt.cpp', 'create() 中用 find_table 判空，不存在则返回 SCHEMA_TABLE_NOT_EXIST'],
        ['sql/executor/drop_table_executor.h', '定义 DropTableExecutor'],
        ['sql/executor/drop_table_executor.cpp', 'execute() 取表名后转交 Db::drop_table'],
        ['sql/stmt/stmt.cpp', '加入 include 与 case SCF_DROP_TABLE'],
        ['sql/executor/command_executor.cpp', '加入 include 与 case StmtType::DROP_TABLE'],
        ['storage/db/db.h', '声明 RC drop_table(const char *table_name)'],
        ['storage/db/db.cpp', '实现三件事：erase 哈希表、delete 对象、unlink 磁盘文件'],
    ], [3300, 5000]))

    o.append(H('3.2.3 存储层核心实现'))
    o.append(P('真正有技术含量的只有 Db::drop_table 一处。它必须做对三件事，缺一不可：'
               '从 opened_tables_ 中 erase（否则 show tables 仍可见）、'
               'delete 表对象（触发 Table → HeapTableEngine 的连环析构，释放文件句柄、缓冲池页与索引对象）、'
               '删除磁盘文件（否则重启后表复活且无法重建）。顺序上必须"先摘哈希表再析构"，'
               '且析构之后只能使用入参 table_name，不能再用表对象的任何成员。'))
    o.append(CODE([
        'Table *table = iter->second;',
        'opened_tables_.erase(iter);      // ① 先从内存索引中摘掉',
        'delete table;                    // ② 析构：关文件、释放缓冲池、释放索引',
        'table = nullptr;                 //    之后只能用入参 table_name，不能再碰 table',
        '',
        '// ③ 删除磁盘文件：<t>.table / <t>.data / <t>.lob / <t>-<idx>.index',
        'const string dot_prefix  = string(table_name) + ".";',
        'const string dash_prefix = string(table_name) + "-";',
        'for (const auto &entry : filesystem::directory_iterator(path_.c_str(), ec)) {',
        '  const string filename = entry.path().filename().string();',
        '  bool matched = (filename.compare(0, dot_prefix.size(), dot_prefix) == 0);',
        '  if (!matched)',
        '    matched = (filename.compare(0, dash_prefix.size(), dash_prefix) == 0 &&',
        '               filename.compare(filename.size() - 6, 6, ".index") == 0);',
        '  if (matched) filesystem::remove(entry.path(), rm_ec);',
        '}',
    ]))
    o.append(P('这里没有直接拼文件名，而是遍历目录做前缀匹配，原因是索引文件的个数与名字在运行时才知道。'
               '用 "表名." 与 "表名-" 两个前缀，既能把 .table/.data/.lob 与任意数量的索引文件一网打尽，'
               '又不会误删 class_room 这类前缀相同但名字不同的表。'))

    o.append(H('3.2.4 编译'))
    o.append(P('由于官方镜像已把第三方库装在 /usr/local（CMakeLists.txt 的 CMAKE_PREFIX_PATH 已包含该路径），'
               '只需执行增量编译，不必也不应执行 build.sh init——容器内访问不到 github.com，'
               '而 build.sh 的 submodule 更新语句带 "|| return"，失败时会静默跳过所有依赖编译。'))
    o.append(CODE([
        'cd /workspaces/miniob',
        './build.sh debug',
        '# 输出末行 [100%] Built target ring_buffer_test 即成功',
        '# observer 二进制大小由 48,105,136 字节变为 48,680,320 字节',
    ]))

    o.append(H('3.2.5 功能验证'))
    o.append(P('按验收用例的六组情形逐条验证，重点覆盖"删后重建""重复删除""带索引的表"三种边界。'))
    o.append(TABLE([
        ['组', '操作', '预期', '实测'],
        ['1', '删除空表', '成功', 'SUCCESS'],
        ['2', '删除有数据的表', '成功', 'SUCCESS'],
        ['3', '删除后向该表 insert / select / delete', '均报错，表已不存在', '均 FAILURE'],
        ['3', '删除后重新 create 同名表', '成功（证明 .table 已删）', 'SUCCESS'],
        ['4', '连续两次 drop 同一张表', '第二次报错', 'FAILURE'],
        ['4', 'drop 一个从未建立过的表', '报错', 'FAILURE'],
        ['5', '删除后重建并查询', '返回空结果集', '结果集 0 行'],
        ['6', '删除带索引的表，再重建同名表', '成功（证明 .index 已删）', 'SUCCESS'],
    ], [600, 3900, 2100, 1700]))

    o.append(H('3.2.6 结果分析'))
    o.append(P('六组用例全部通过，其中第 3 组和第 5 组的"删除后重建同名表成功"这一条是最关键的证据：'
               '它同时证明了内存哈希表已摘除、.table 元数据文件已删除（否则 O_EXCL 会报 SCHEMA_TABLE_EXIST）、'
               '以及数据文件与索引文件已一并清理。'))
    o.append(P('另外观察到 command_executor.cpp 中的收尾逻辑对本次实现是"白送"的：'
               'stmt_type_ddl() 在原始代码中就已经把 DROP_TABLE 列在 DDL 集合里，'
               '因此 drop table 成功后会自动触发一次 db->sync() 把元数据刷盘，保证了持久性，无需额外编写代码。'))
    o.append(NOTE('过程中的一个说明：drop table 未实现之前，同样的语句返回的是 ERROR 2 (HY000): UNIMPLEMENTED，'
                  '这类错误属于"语法和语义都通过、但执行层没实现"；'
                  '实现之后失败情形返回的是 FAILURE（对应 SCHEMA_TABLE_NOT_EXIST）。'
                  '两种报错的区别可以反过来印证执行链已经走到了哪一层。'))

    o.append(H('3.3 过程量化记录'))
    o.append(TABLE([
        ['项目', '实测值', '说明'],
        ['首次全量编译耗时', '约 2863 秒（48 分钟）', '1038 个源文件，瓶颈是 Windows 挂载盘的跨虚拟化文件 I/O'],
        ['增量编译耗时', '约 30 秒—2 分钟', '本次改动 8 个文件，仅重编对应目标并重新链接'],
        ['observer 体积变化', '48105136 → 48680320 字节', '增加约 561 KB，即新增代码编译后的体积'],
        ['单元测试数量', '44 个', 'build_debug/unittest 下，可用 ctest 批量运行'],
        ['功能用例覆盖', '六组共 8 条断言', '对应 primary-drop-table.test 的六组情形'],
    ], [2200, 2900, 3200]))
    return ''.join(o)


# ============================================================ 四、试验完成情况
def sec_done():
    o = []
    o.append(P('本次实验安排的两项任务均已完成并通过自测，具体完成情况如下。'))
    o.append(TABLE([
        ['任务', '完成情况', '验收证据'],
        ['任务 1：验证 SELECT 语句的表名与字段名',
         '已完成。给出四组用例，覆盖表名正确、表名错误、字段名错误、空结果四种情形，'
         '并从源码中定位了校验位置与错误码。',
         '四组用例结果全部符合预期；表名错误对应 SCHEMA_TABLE_NOT_EXIST，'
         '字段名错误对应 SCHEMA_FIELD_MISSING'],
        ['任务 2：实现 drop table',
         '已完成。新建 4 个文件、修改 4 个文件，补齐语义层、执行层与存储层，'
         '并额外实现了 default_handler 中的同名接口以保持代码一致。',
         'primary-drop-table.test 的六组情形（空表、有数据表、删后重建、重复删除、'
         '重建后查询、带索引表）全部通过'],
    ], [2300, 3200, 2800]))
    o.append(P('此外，为便于后续复习与同学参考，还在实现链路的关键位置补充了详细注释，'
               '覆盖 create table 与 drop table 两条链路的 8 处核心位置，'
               '注释内容侧重于"为什么这样可以工作"，包括 DDL 与 DML 的分流机制、'
               '哈希表作为"表存在"定义的原因、RAII 析构如何完成内存清理、'
               '以及磁盘文件为何必须手工删除等。'))
    o.append(P('未完成或未覆盖的部分：本次实验未涉及 update、alter table、date 类型与聚合函数，'
               '这些属于后续三次实验的任务；'
               '也未对并发场景下的 drop table 做保护（原始代码本身没有表锁，'
               'db.h 的类注释已明确说明这一限制由调用者保证）。'))
    return ''.join(o)


# ============================================================ 五、试验总结
def sec_summary():
    o = []
    o.append(H('5.1 遇到的问题及解决措施'))
    o.append(TABLE([
        ['序号', '问题现象', '原因', '解决措施'],
        ['1', 'git clone 与 curl 报 schannel: SEC_E_NO_CREDENTIALS',
         '计划任务环境下 TLS 凭据获取受限',
         'git 显式指定 http.sslBackend=openssl；依赖改从镜像源获取'],
        ['2', 'wsl --update 长时间停留在"正在下载 3.0.3"',
         '商店投递优化通道超时',
         '改用 winget 从 msstore 源安装 WSL 包，成功升级到 3.0.1.0'],
        ['3', 'docker pull 官方镜像看起来卡住',
         'containerd 在后台继续拉取，CLI 被中止后仍在进行',
         '等待后台完成；并配置 registry-mirrors 加速'],
        ['4', '容器内构建镜像失败（连不上 github.com）',
         '容器网络无法访问 github，而 apt 与国内镜像可用',
         '改用本地工具链镜像，第三方库复用官方镜像预装的 /usr/local'],
        ['5', 'build.sh debug 报回车符错误（command not found）',
         '系统 core.autocrlf=true，1038 个源文件全部变成 CRLF',
         '将仓库与 5 个子模块统一转为 LF，并设置 core.autocrlf=false'],
        ['6', '在 miniob> 提示符下敲 shell 命令报 Failed to parse sql',
         '混淆了数据库提示符与 Linux shell 提示符',
         '约定用两个终端：一个常驻 miniob> 敲 SQL，另一个敲 shell 命令'],
        ['7', 'select 任意表都返回 FAILURE',
         '数据目录随工作目录变化，换目录启动得到了空库',
         '用 show tables 判断当前库是否为空，并固定在同一目录启动'],
        ['8', '插入的数据在重启后丢失',
         '默认 vacuous 模式不保证持久化',
         '启动参数加 -d disk'],
    ], [500, 2700, 2600, 2600]))

    o.append(H('5.2 对本次实验的评价'))
    o.append(P('从任务设计看，这两项任务形成一个很好的递进：'
               '任务 1 是"读"，要求把 SELECT 的校验链路查清楚并用用例证明它是对的；'
               '任务 2 是"写"，要求在读懂 create table 全链路的基础上按镜像结构自己补一条新的语句。'
               '前者训练定位能力，后者训练迁移能力，两者结合才真正理解了一条 SQL 在数据库内部是怎么被处理的。'))
    o.append(P('题目难度设置合理。drop table 只需改动 8 个文件、约 150 行代码，'
               '但每一处改动都必须想清楚职责归属：'
               '为什么存在性检查放在 Stmt 层而不是 Executor 层，'
               '为什么 Executor 只做转发，为什么内存清理可以交给析构而磁盘清理必须手写。'
               '这些问题想不清楚，代码能跑但不可靠——比如只摘哈希表不删文件，'
               '测试用例第 3、5 组就会失败，而且失败现象（重启后表复活）非常隐蔽。'))
    o.append(P('不足之处在于 MiniOB 的文档与代码已有一定脱节，'
               '例如 parse_defs.h 中的语法与结构体早已写好，但语义层与执行层是空的，'
               '初学者容易误以为"语法有了就等于功能有了"。'
               '另外绝大部分测试用例不再附带期望结果文件，需要自己对照 MySQL 8.0 判断，'
               '这增加了验收成本，但也逼迫我们去理解正确行为本身。'))

    o.append(H('5.3 感想和认识'))
    o.append(P('第一点认识是关于"分层"的价值。'
               '在没读源码之前，实现一个 drop table 直觉上是一件"删文件"的事；'
               '读完之后才明白，在数据库里"表存在"这个状态被同时表达在三个地方：'
               '内存哈希表、磁盘元数据文件、以及存储引擎持有的句柄与缓冲池。'
               '只有三处一致地清理掉，删除才是真的删除。'
               '这也反过来解释了为什么数据库的 DDL 往往比看起来要复杂——'
               '它必须同时维护逻辑视图和物理视图的一致性。'))
    o.append(P('第二点认识是关于"参照已有实现"这一方法本身。'
               '指导书建议"处理 drop table 就参照 create table"，这句话的实际含金量很高：'
               '不是照抄代码，而是照着它的分层结构去找每一层需要什么，'
               '再写出方向相反的操作。'
               'create 是"查重 → 建对象 → 写文件 → 挂引擎"，'
               'drop 就是"查存在 → 摘对象 → 删文件 → 析构引擎"。'
               '一旦看出这层镜像关系，实现就变成了填空题。'))
    o.append(P('第三点认识是关于工具链和环境。'
               '这次实验里真正花时间的部分并不是写 drop table 的那 150 行代码，'
               '而是把 Windows + WSL2 + Docker + VS Code 这套环境调通，'
               '以及在遇到换行符、镜像源、工作目录这类问题时能把原因找出来。'
               '这些问题看似与数据库无关，但它们决定了后续三次实验能否顺利开展。'
               '从这个角度看，实验一最有价值的产出其实是那套可复现的环境与工作方式，'
               '而不只是 drop table 这个功能本身。'))
    return ''.join(o)


def all_sections():
    return [sec_task(), sec_prepare(), sec_process(), sec_done(), sec_summary()]
