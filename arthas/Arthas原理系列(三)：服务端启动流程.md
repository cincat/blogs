## 前言

本篇文章主要讲我们在终端中敲入的命令是如何被 arthas 服务器识别并且解释的。要注意这个过程是 arthas 对所有命令执行过程的抽闲个，对于具体命令的执行过程我会在后面的系列文章中再说。

## arthas 服务端的启动

在上一篇文章中，我们跟踪了整个 arthas 工程的入口方法：`com.taobao.arthas.agent334.AgentBootstrap#main`，在这个方法中，最重要的一个步骤就是启动过了一个绑定线程

```java
private static synchronized void main(String args, final Instrumentation inst) {
    try {
        // 1. 程序运行前的校验，
        // arthas如果已经存在，则直接返回
        // 入参中必须要包含arthas core等
        // 这些代码细节不会影响我们对主流程的理解，因此暂时删除
        final ClassLoader agentLoader = getClassLoader(inst, arthasCoreJarFile);
        Thread bindingThread = new Thread() {
            @Override
            public void run() {
                try {
                    bind(inst, agentLoader, agentArgs);
                } catch (Throwable throwable) {
                    throwable.printStackTrace(ps);
                }
            }
        };

        bindingThread.setName("arthas-binding-thread");
        bindingThread.start();
        bindingThread.join();
    } catch (Throwable t) {
        t.printStackTrace(ps);
        try {
            if (ps != System.err) {
                ps.close();
            }
        } catch (Throwable tt) {
            // ignore
        }
        throw new RuntimeException(t);
    }
```

`bind`这个线程的运行时会调用`com.taobao.arthas.agent334.AgentBootstrap#bind`，这个方法的详细代码如下：

```java
private static void bind(Instrumentation inst, ClassLoader agentLoader, String args) throws Throwable {
        /**
         * <pre>
         * ArthasBootstrap bootstrap = ArthasBootstrap.getInstance(inst);
         * </pre>
         */
        Class<?> bootstrapClass = agentLoader.loadClass(ARTHAS_BOOTSTRAP);
        Object bootstrap = bootstrapClass.getMethod(GET_INSTANCE, Instrumentation.class, String.class).invoke(null, inst, args);
        boolean isBind = (Boolean) bootstrapClass.getMethod(IS_BIND).invoke(bootstrap);
        if (!isBind) {
            String errorMsg = "Arthas server port binding failed! Please check $HOME/logs/arthas/arthas.log for more details.";
            ps.println(errorMsg);
            throw new RuntimeException(errorMsg);
        }
        ps.println("Arthas server already bind.");
    }
```

这段方法用反射的方法调用了`com.taobao.arthas.core.server.ArthasBootstrap`的静态方法`getInstance`，并且把从`main`方法中解析到参数再传到这个`getInstance`中。

`getInstance`从这个名字看就是返回一个`ArthasBootstrap`的实例，事实上代码的逻辑也是这样的，其中最关键的就是`ArthasBootstrap`的构造函数函数：

```java
private ArthasBootstrap(Instrumentation instrumentation, Map<String, String> args) throws Throwable {
        this.instrumentation = instrumentation;

        String outputPath = System.getProperty("arthas.output.dir", "arthas-output");
        arthasOutputDir = new File(outputPath);
        arthasOutputDir.mkdirs();

        // 1. initSpy()
        // 加载SpyAPI这个类
        initSpy(instrumentation);
        // 2. ArthasEnvironment
        // 初始化arthas运行的环境变量
        initArthasEnvironment(args);
        // 3. init logger
        loggerContext = LogUtil.initLooger(arthasEnvironment);

        // 4. init beans
        // 初始化结果渲染和历史命令管理的相关类
        initBeans();

        // 5. start agent server
        // 启动server，开始监听
        bind(configure);

        // 注册一些钩子函数
        executorService = Executors.newScheduledThreadPool(1, new ThreadFactory() {
            @Override
            public Thread newThread(Runnable r) {
                final Thread t = new Thread(r, "arthas-command-execute");
                t.setDaemon(true);
                return t;
            }
        });

        shutdown = new Thread("as-shutdown-hooker") {

            @Override
            public void run() {
                ArthasBootstrap.this.destroy();
            }
        };

        transformerManager = new TransformerManager(instrumentation);
        Runtime.getRuntime().addShutdownHook(shutdown);
    }
```

在这个构造函数中，最重要的就是`com.taobao.arthas.core.server.ArthasBootstrap#bind`这个方法

```java
private void bind(Configure configure) throws Throwable {

    // 无关紧要的一些前置操作，先删除掉

    try {
        // 关于arthas tunnel server，请参考：
        // https://arthas.aliyun.com/doc/tunnel.html
        if (configure.getTunnelServer() != null) {
            tunnelClient = new TunnelClient();
            tunnelClient.setAppName(configure.getAppName());
            tunnelClient.setId(configure.getAgentId());
            tunnelClient.setTunnelServerUrl(configure.getTunnelServer());
            tunnelClient.setVersion(ArthasBanner.version());
            ChannelFuture channelFuture = tunnelClient.start();
            channelFuture.await(10, TimeUnit.SECONDS);
        }
    } catch (Throwable t) {
        logger().error("start tunnel client error", t);
    }

    try {
        // 将一些非常关键的参数包装成ShellServerOptions对象
        ShellServerOptions options = new ShellServerOptions()
                        .setInstrumentation(instrumentation)
                        .setPid(PidUtils.currentLongPid())
                        .setWelcomeMessage(ArthasBanner.welcome());
        if (configure.getSessionTimeout() != null) {
            options.setSessionTimeout(configure.getSessionTimeout() * 1000);
        }

        // new 一个shellServer，用于监听命令
        shellServer = new ShellServerImpl(options);

        // BuiltinCommandPack对象首次出现，包含了所有的内置命令
        BuiltinCommandPack builtinCommands = new BuiltinCommandPack();
        List<CommandResolver> resolvers = new ArrayList<CommandResolver>();
        resolvers.add(builtinCommands);

        //worker group
        workerGroup = new NioEventLoopGroup(new DefaultThreadFactory("arthas-TermServer", true));

        // TODO: discover user provided command resolver
        if (configure.getTelnetPort() != null && configure.getTelnetPort() > 0) {
            shellServer.registerTermServer(new HttpTelnetTermServer(configure.getIp(), configure.getTelnetPort(),
                    options.getConnectionTimeout(), workerGroup));
        } else {
            logger().info("telnet port is {}, skip bind telnet server.", configure.getTelnetPort());
        }
        if (configure.getHttpPort() != null && configure.getHttpPort() > 0) {
            shellServer.registerTermServer(new HttpTermServer(configure.getIp(), configure.getHttpPort(),
                    options.getConnectionTimeout(), workerGroup));
        } else {
            // listen local address in VM communication
            if (configure.getTunnelServer() != null) {
                shellServer.registerTermServer(new HttpTermServer(configure.getIp(), configure.getHttpPort(),
                        options.getConnectionTimeout(), workerGroup));
            }
            logger().info("http port is {}, skip bind http server.", configure.getHttpPort());
        }

        for (CommandResolver resolver : resolvers) {
            shellServer.registerCommandResolver(resolver);
        }

        shellServer.listen(new BindHandler(isBindRef));
        if (!isBind()) {
            throw new IllegalStateException("Arthas failed to bind telnet or http port.");
        }

        //http api session manager
        sessionManager = new SessionManagerImpl(options, shellServer.getCommandManager(), shellServer.getJobController());
        //http api handler
        httpApiHandler = new HttpApiHandler(historyManager, sessionManager);

        logger().info("as-server listening on network={};telnet={};http={};timeout={};", configure.getIp(),
                configure.getTelnetPort(), configure.getHttpPort(), options.getConnectionTimeout());

        // 异步回报启动次数
        if (configure.getStatUrl() != null) {
            logger().info("arthas stat url: {}", configure.getStatUrl());
        }
        UserStatUtil.setStatUrl(configure.getStatUrl());
        UserStatUtil.arthasStart();

        try {
            SpyAPI.init();
        } catch (Throwable e) {
            // ignore
        }

        logger().info("as-server started in {} ms", System.currentTimeMillis() - start);
    } catch (Throwable e) {
        logger().error("Error during start as-server", e);
        destroy();
        throw e;
    }
}
```

这个方法使我们到目前为止见到的最复杂的一个方法，里面还是有很多的旁枝末节的干扰，总结一下，这个方法全都是围绕着如何构建一个`ShellServer`对象来进行的：

1. 第一步会将一些非常重要的入参包装`ShellServerOptions`传入`ShellServer`
2. 然后会在`ShellerServer`上注册命令解释器`BuiltinCommandPack`，点开`BuiltinCommandPack`会发现，所有的命令都已经包含在内了
3. 根据入参的不同在`ShellerServer`上注册不同的`TermServer`，比如`HttpTermServer`或者是`HttpTelnetTermServer`
4. 服务器开启监听指令

`BuiltinCommandPack`的实现如下所示：

```java
public class  BuiltinCommandPack implements CommandResolver {

    private static List<Command> commands = new ArrayList<Command>();

    static {
        initCommands();
    }

    @Override
    public List<Command> commands() {
        return commands;
    }

    private static void initCommands() {
        commands.add(Command.create(HelpCommand.class));
        commands.add(Command.create(KeymapCommand.class));
        commands.add(Command.create(SearchClassCommand.class));
        commands.add(Command.create(SearchMethodCommand.class));
        // ...
    }
}
```

## 服务端对命令行的监听和处理

接下来我们分析arthas服务端的监听过程
```java
@Override
public ShellServer listen(final Handler<Future<Void>> listenHandler) {
    final List<TermServer> toStart;
    synchronized (this) {
        if (!closed) {
            throw new IllegalStateException("Server listening");
        }
        toStart = termServers;
    }
    final AtomicInteger count = new AtomicInteger(toStart.size());
    if (count.get() == 0) {
        setClosed(false);
        listenHandler.handle(Future.<Void>succeededFuture());
        return this;
    }
    Handler<Future<TermServer>> handler = new TermServerListenHandler(this, listenHandler, toStart);
    for (TermServer termServer : toStart) {
        // termHandler是termServer监听命令的回调函数
        // 当有新的命令通过网络到达server时会调用这个回调函数
        termServer.termHandler(new TermServerTermHandler(this));
        termServer.listen(handler);
    }
    return this;
}
```

我们以`HttpTermServer`为例
```java
@Override
public TermServer listen(Handler<Future<TermServer>> listenHandler) {
    // TODO: charset and inputrc from options
    bootstrap = new NettyWebsocketTtyBootstrap(workerGroup).setHost(hostIp).setPort(port);
    try {
        bootstrap.start(new Consumer<TtyConnection>() {
            @Override
            public void accept(final TtyConnection conn) {
                termHandler.handle(new TermImpl(Helper.loadKeymap(), conn));
            }
        }).get(connectionTimeout, TimeUnit.MILLISECONDS);
        listenHandler.handle(Future.<TermServer>succeededFuture());
    } catch (Throwable t) {
        logger.error("Error listening to port " + port, t);
        listenHandler.handle(Future.<TermServer>failedFuture(t));
    }
    return this;
}
```
会发现程序会最终去异步的调用`termHandler`的`handle`方法，而`termHandler`正是前面注册的`TermServerTermHandler`这个类的实例：
```java
public class TermServerTermHandler implements Handler<Term> {
    private ShellServerImpl shellServer;

    public TermServerTermHandler(ShellServerImpl shellServer) {
        this.shellServer = shellServer;
    }

    @Override
    public void handle(Term term) {
        shellServer.handleTerm(term);
    }
}
```
`handle`又回调了`shellServer`的`handleTerm`方法，我们的视线随着调用流程再回到`ShellServer`这个类
```java
public void handleTerm(Term term) {
    synchronized (this) {
        // That might happen with multiple ser
        if (closed) {
            term.close();
            return;
        }
    }

    ShellImpl session = createShell(term);
    tryUpdateWelcomeMessage();
    session.setWelcome(welcomeMessage);
    session.closedFuture.setHandler(new SessionClosedHandler(this, session));
    session.init();
    sessions.put(session.id, session); // Put after init so the close handler on the connection is set
    session.readline(); // Now readline
}
```
这个方法中的最后一行代码`session.readline();`是我们重点要关注的地方
```java
public void readline() {
    // 这里要注意ShellLineHandler这个类，后面readLine的回调最终会回到这里来
    term.readline(prompt, new ShellLineHandler(this),
            new CommandManagerCompletionHandler(commandManager));
}
```
我们以`TermImpl`的实现为例
```java
public void readline(String prompt, Handler<String> lineHandler, Handler<Completion> completionHandler) {
    if (conn.getStdinHandler() != echoHandler) {
        throw new IllegalStateException();
    }
    if (inReadline) {
        throw new IllegalStateException();
    }
    inReadline = true;
    readline.readline(conn, prompt, new RequestHandler(this, lineHandler), new CompletionHandler(completionHandler, session));
}
```

这个方法调用了`readline.readline`方法，并把之前传进来的`ShellLineHandler`也包进了`RequestHandler`传到了`readline.readline`中，我们继续往进看
```java
public void readline(TtyConnection conn, String prompt, Consumer<String> requestHandler, Consumer<Completion> completionHandler) {
    synchronized (this) {
        if (interaction != null) {
        throw new IllegalStateException("Already reading a line");
        }
        interaction = new Interaction(conn, prompt, requestHandler, completionHandler);
    }
    interaction.install();
    conn.write(prompt);
    schedulePendingEvent();
}
```

`interaction.install();`以及`schedulePendingEvent();`这两汉代码最终都会调用下面的一段方法
```java
private void deliver() {
    while (true) {
      Interaction handler;
      KeyEvent event;
      synchronized (this) {
        if (decoder.hasNext() && interaction != null && !interaction.paused) {
          event = decoder.next();
          handler = interaction;
        } else {
          return;
        }
      }
      handler.handle(event);
    }
}
```
`Interaction`是`ReadLine`的一个内部类，他的handle方法比较长，我们截取这个方法的关键部分如下所示：
```java
private void handle(KeyEvent event) {
    if (event instanceof FunctionEvent) {
    FunctionEvent fname = (FunctionEvent) event;
    Function function = functions.get(fname.name());
    if (function != null) {
        synchronized (this) {
        paused = true;
        }
        function.apply(this);
    } else {
        Logging.READLINE.warn("Unimplemented function " + fname.name());
    }
    } else {
    LineBuffer buf = buffer.copy();
    for (int i = 0;i < event.length();i++) {
        int codePoint = event.getCodePointAt(i);
        try {
        buf.insert(codePoint);
        } catch (IllegalArgumentException e) {
        conn.stdoutHandler().accept(new int[]{'\007'});
        }
    }
    refresh(buf);
    }
}
```
在这段代码中，会首先判断输入时间是否在预存的`functions`这个变量中已经定义，如果有的话，则执行相应`apply`方法，否则做缓存相关的操作。
在ReadLine这个类新建的时候，预定义了一个方法，那就是`ACCEPT_LINE`

```java
public Readline(Keymap keymap) {
    // https://github.com/alibaba/termd/issues/42
    // this.device = TermInfo.defaultInfo().getDevice("xterm"); // For now use xterm
    this.decoder = new EventQueue(keymap);
    this.history = new ArrayList<int[]>();
    addFunction(ACCEPT_LINE);
}
```
`ACCEPT_LINE`的定义如下：
```java
private final Function ACCEPT_LINE = new Function() {

    @Override
    public String name() {
      return "accept-line";
    }

    @Override
    public void apply(Interaction interaction) {
      interaction.line.insert(interaction.buffer.toArray());
      LineStatus pb = new LineStatus();
      for (int i = 0;i < interaction.line.getSize();i++) {
        pb.accept(interaction.line.getAt(i));
      }
      interaction.buffer.clear();
      if (pb.isEscaping()) {
        interaction.line.delete(-1); // Remove \
        interaction.currentPrompt = "> ";
        interaction.conn.write("\n> ");
        interaction.resume();
      } else {
        if (pb.isQuoted()) {
          interaction.line.insert('\n');
          interaction.conn.write("\n> ");
          interaction.currentPrompt = "> ";
          interaction.resume();
        } else {
          String raw = interaction.line.toString();
          if (interaction.line.getSize() > 0) {
            addToHistory(interaction.line.toArray());
          }
          interaction.line.clear();
          interaction.conn.write("\n");
          interaction.end(raw);
        }
      }
    }
}
```
在`ACCEPT_LINE`的`apply`方法中，如果程序判定到达服务器的是一个合法的命令行，则会调用`io.termd.core.readline.Readline.Interaction#end`方法，而这个方法，最终会调用`requestHandler.accept(s);`，这个`RequestHandler`其实就是封装了一层`ShellLineHandler`。
```java
private boolean end(String s) {
    synchronized (Readline.this) {
    if (interaction == null) {
        return false;
    }
    interaction = null;
    conn.setStdinHandler(prevReadHandler);
    conn.setSizeHandler(prevSizeHandler);
    conn.setEventHandler(prevEventHandler);
    }
    requestHandler.accept(s);
    return true;
}
```
通过上面的分析可以看到，后续我们对命令的处理直接看`ShellLineHandler`就可以了

## 命令的执行
```java
public void handle(String line) {
    String name = first.value();
    if (name.equals("exit") || name.equals("logout") || name.equals("q") || name.equals("quit")) {
        handleExit();
        return;
    } else if (name.equals("jobs")) {
        handleJobs();
        return;
    } else if (name.equals("fg")) {
        handleForeground(tokens);
        return;
    } else if (name.equals("bg")) {
        handleBackground(tokens);
        return;
    } else if (name.equals("kill")) {
        handleKill(tokens);
        return;
    }

    Job job = createJob(tokens);
    if (job != null) {
        job.run();
    }
}
```
在`com.taobao.arthas.core.shell.handlers.shell.ShellLineHandler#handle`的设计中，如果是一些简单的命令，比如说`exit, logout,jobs,fg,bg,kill`等，都是直接执行的，而其他的命令都是直接通过创建一个Job来执行的，这一小节，我们主要看arthas是怎么抽象命令的执行的：从创建`Job`开始
```java
@Override
public synchronized Job createJob(List<CliToken> args) {
    Job job = jobController.createJob(commandManager, args, session, new ShellJobHandler(this), term, null);
    return job;
}
```
会转发到：
```java
@Override
public Job createJob(InternalCommandManager commandManager, List<CliToken> tokens, Session session, JobListener jobHandler, Term term, ResultDistributor resultDistributor) {
    int jobId = idGenerator.incrementAndGet();
    StringBuilder line = new StringBuilder();
    for (CliToken arg : tokens) {
        line.append(arg.raw());
    }
    boolean runInBackground = runInBackground(tokens);
    Process process = createProcess(tokens, commandManager, jobId, term, resultDistributor);
    process.setJobId(jobId);
    JobImpl job = new JobImpl(jobId, this, process, line.toString(), runInBackground, session, jobHandler);
    jobs.put(jobId, job);
    return job;
}
```
`Job`的`run`方法是完全委托给`Process`的，所以接下来就直接看`createProcess`的过程：
```java
private Process createProcess(List<CliToken> line, InternalCommandManager commandManager, int jobId, Term term, ResultDistributor resultDistributor) {
    try {
        ListIterator<CliToken> tokens = line.listIterator();
        while (tokens.hasNext()) {
            CliToken token = tokens.next();
            if (token.isText()) {
                Command command = commandManager.getCommand(token.value());
                if (command != null) {
                    return createCommandProcess(command, tokens, jobId, term, resultDistributor);
                } else {
                    throw new IllegalArgumentException(token.value() + ": command not found");
                }
            }
        }
        throw new IllegalArgumentException();
    } catch (Exception e) {
        throw new RuntimeException(e);
    }
}
```
这段代码的意图比较明显，根据输入的命令去找相应的`Command`对象，如果找到则创建`Process`对象，根据文本找相应`Command`的逻辑如下：
```Java
public Command getCommand(String commandName) {
    Command command = null;
    for (CommandResolver resolver : resolvers) {
        // 内建命令在ShellLineHandler里提前处理了，所以这里不需要再查找内建命令
        if (resolver instanceof BuiltinCommandPack) {
            command = getCommand(resolver, commandName);
            if (command != null) {
                break;
            }
        }
    }
    return command;
}

private static Command getCommand(CommandResolver commandResolver, String name) {
    List<Command> commands = commandResolver.commands();
    if (commands == null || commands.isEmpty()) {
        return null;
    }

    for (Command command : commands) {
        if (name.equals(command.name())) {
            return command;
        }
    }
    return null;
}
```
这块的逻辑还是比较清晰的，我们再看看在找到对应`Command`之后如何创建`Process`
```java
private Process createCommandProcess(Command command, ListIterator<CliToken> tokens, int jobId, Term term, ResultDistributor resultDistributor) throws IOException {
    List<CliToken> remaining = new ArrayList<CliToken>();
    List<CliToken> pipelineTokens = new ArrayList<CliToken>();
    boolean isPipeline = false;
    RedirectHandler redirectHandler = null;
    List<Function<String, String>> stdoutHandlerChain = new ArrayList<Function<String, String>>();
    String cacheLocation = null;
    // 删除中间处理管道和后台进程的代码
    ProcessOutput ProcessOutput = new ProcessOutput(stdoutHandlerChain, cacheLocation, term);
    ProcessImpl process = new ProcessImpl(command, remaining, command.processHandler(), ProcessOutput, resultDistributor);
    process.setTty(term);
    return process;
}
```
在删除了中间的处理管道和后台命令的代码之后这段代码的逻辑也非常清晰，就是根据解析好的`Command`对象创建一个`Process`对象，值得注意的是，这里把`command.processHandler()`传进了`Process`的构造函数中。查看`com.taobao.arthas.core.shell.system.impl.ProcessImpl#run()`，可以看到最终会调用到`com.taobao.arthas.core.shell.system.impl.ProcessImpl.CommandProcessTask#run`
```java
private class CommandProcessTask implements Runnable {

    private CommandProcess process;

    public CommandProcessTask(CommandProcess process) {
        this.process = process;
    }

    @Override
    public void run() {
        try {
            handler.handle(process);
        } catch (Throwable t) {
            logger.error("Error during processing the command:", t);
            process.end(1, "Error during processing the command: " + t.getClass().getName() + ", message:" + t.getMessage()
                    + ", please check $HOME/logs/arthas/arthas.log for more details." );
        }
    }
}
```
这里的`handler`正是创建`Process`对象时调用`command.processHandler()`传进去的

```java
// processHandler 初始化
private Handler<CommandProcess> processHandler = new ProcessHandler();
@Override
public Handler<CommandProcess> processHandler() {
    return processHandler;
}

private class ProcessHandler implements Handler<CommandProcess> {
    @Override
    public void handle(CommandProcess process) {
        process(process);
    }
}

private void process(CommandProcess process) {
    AnnotatedCommand instance;
    try {
        instance = clazz.newInstance();
    } catch (Exception e) {
        process.end();
        return;
    }
    CLIConfigurator.inject(process.commandLine(), instance);
    instance.process(process);
    UserStatUtil.arthasUsageSuccess(name(), process.args());
}
```
通过`instance.process(process);`就可以调用到具体`Command`类的`process`方法了，比如说我们以`watch`命令为例，如果客户端输入的是这条命令，则会触发代码的插装
```java
@Override
public void process(final CommandProcess process) {
    // ctrl-C support
    process.interruptHandler(new CommandInterruptHandler(process));
    // q exit support
    process.stdinHandler(new QExitHandler(process));

    // start to enhance
    enhance(process);
}
```
## 小结一下

整个启动过程还是比较清晰的，需要注意的是在这个过程中有好多的回调函数，这些回调函数中才包含真正处理事件的逻辑，需要多翻几遍上下文才能完全理解


![img](https://gitee.com/cincat/picgo/raw/master/img/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0REX0RkZGQ=,size_16,color_FFFFFF,t_70.png)

本文详细的跟了上面这个类图中类之间的交互，服务器抽象这个模块主要负责建立起完整的服务器环境并监听到达服务端的命令，到达的命令经过初步解析之后通过建立的任务类去执行，在任务的执行中通过在`ShellImpl`中持有的`ShellServer`引用，可以解析出具体的`Command`类，最后，命令的执行会调用对应`Command`类中的`process`方法，从而完成了整个命令的执行。