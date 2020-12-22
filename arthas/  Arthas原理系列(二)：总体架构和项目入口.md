## Arthas 启动入口

我们先从启动脚本`as.sh`看起，在这个脚本中首先会去列 Java 程序的 PID，用户选择之后再`attach`到目标 JVM 上，相关的代码是：

```shell
    "${java_command[@]}" \
        ${ARTHAS_OPTS} ${JVM_OPTS} \
        -jar "${arthas_lib_dir}/arthas-core.jar" \
            -pid ${TARGET_PID} \
            -target-ip ${TARGET_IP} \
            -telnet-port ${TELNET_PORT} \
            -http-port ${HTTP_PORT} \
            -session-timeout ${SESSION_TIMEOUT} \
            "${tempArgs[@]}" \
            -core "${arthas_lib_dir}/arthas-core.jar" \
            -agent "${arthas_lib_dir}/arthas-agent.jar"
```

在这里我们可以看到 arths 的入口是`arthas-core.jar`我们在项目工程`core`这个目录下果然找到`com.taobao.arthas.core.Arthas#main`这个入口函数，果然，程序兜兜转转会调用到`attachAgent`，而这个方法，和我们前一节的示例程序流程大体一致，都是最终把一个`agent`挂载到目标`JVM`上

```Java
private void attachAgent(Configure configure) throws Exception {
        VirtualMachineDescriptor virtualMachineDescriptor = null;
        for (VirtualMachineDescriptor descriptor : VirtualMachine.list()) {
            String pid = descriptor.id();
            if (pid.equals(Long.toString(configure.getJavaPid()))) {
                virtualMachineDescriptor = descriptor;
                break;
            }
        }
        VirtualMachine virtualMachine = null;
        try {
            if (null == virtualMachineDescriptor) { // 使用 attach(String pid) 这种方式
                virtualMachine = VirtualMachine.attach("" + configure.getJavaPid());
            } else {
                virtualMachine = VirtualMachine.attach(virtualMachineDescriptor);
            }

            Properties targetSystemProperties = virtualMachine.getSystemProperties();
            String targetJavaVersion = JavaVersionUtils.javaVersionStr(targetSystemProperties);
            String currentJavaVersion = JavaVersionUtils.javaVersionStr();
            if (targetJavaVersion != null && currentJavaVersion != null) {
                if (!targetJavaVersion.equals(currentJavaVersion)) {
                    AnsiLog.warn("Current VM java version: {} do not match target VM java version: {}, attach may fail.",
                                    currentJavaVersion, targetJavaVersion);
                    AnsiLog.warn("Target VM JAVA_HOME is {}, arthas-boot JAVA_HOME is {}, try to set the same JAVA_HOME.",
                                    targetSystemProperties.getProperty("java.home"), System.getProperty("java.home"));
                }
            }

            String arthasAgentPath = configure.getArthasAgent();
            //convert jar path to unicode string
            configure.setArthasAgent(encodeArg(arthasAgentPath));
            configure.setArthasCore(encodeArg(configure.getArthasCore()));
            // 加载代理，代理路径从命令行中解析
            virtualMachine.loadAgent(arthasAgentPath,
                    configure.getArthasCore() + ";" + configure.toString());
        } finally {
            if (null != virtualMachine) {
                virtualMachine.detach();
            }
        }
    }
```

在 JVM 的 attach 机制中全都是 agent 在干活，如果我们能找到 agent，就相当于找到了整个程序的入口，可以看到，挂载 agent 的路径是`arthasAgentPath`，这个值是在上一步解析参数的时候得来的：

```java
private Configure parse(String[] args) {
        Option pid = new TypedOption<Long>().setType(Long.class).setShortName("pid").setRequired(true);
        Option core = new TypedOption<String>().setType(String.class).setShortName("core").setRequired(true);
        Option agent = new TypedOption<String>().setType(String.class).setShortName("agent").setRequired(true);
        Option target = new TypedOption<String>().setType(String.class).setShortName("target-ip");
        Option telnetPort = new TypedOption<Integer>().setType(Integer.class)
                .setShortName("telnet-port");
        Option httpPort = new TypedOption<Integer>().setType(Integer.class)
                .setShortName("http-port");
        Option sessionTimeout = new TypedOption<Integer>().setType(Integer.class)
                        .setShortName("session-timeout");

        Option tunnelServer = new TypedOption<String>().setType(String.class).setShortName("tunnel-server");
        Option agentId = new TypedOption<String>().setType(String.class).setShortName("agent-id");
        Option appName = new TypedOption<String>().setType(String.class).setShortName(ArthasConstants.APP_NAME);

        Option statUrl = new TypedOption<String>().setType(String.class).setShortName("stat-url");

        CLI cli = CLIs.create("arthas").addOption(pid).addOption(core).addOption(agent).addOption(target)
                .addOption(telnetPort).addOption(httpPort).addOption(sessionTimeout).addOption(tunnelServer).addOption(agentId).addOption(appName).addOption(statUrl);
        CommandLine commandLine = cli.parse(Arrays.asList(args));

        Configure configure = new Configure();
        configure.setJavaPid((Long) commandLine.getOptionValue("pid"));
        // agent解析
        configure.setArthasAgent((String) commandLine.getOptionValue("agent"));
        configure.setArthasCore((String) commandLine.getOptionValue("core"));
        if (commandLine.getOptionValue("session-timeout") != null) {
            configure.setSessionTimeout((Integer) commandLine.getOptionValue("session-timeout"));
        }

        if (commandLine.getOptionValue("target-ip") != null) {
            configure.setIp((String) commandLine.getOptionValue("target-ip"));
        }

        if (commandLine.getOptionValue("telnet-port") != null) {
            configure.setTelnetPort((Integer) commandLine.getOptionValue("telnet-port"));
        }
        if (commandLine.getOptionValue("http-port") != null) {
            configure.setHttpPort((Integer) commandLine.getOptionValue("http-port"));
        }

        configure.setTunnelServer((String) commandLine.getOptionValue("tunnel-server"));
        configure.setAgentId((String) commandLine.getOptionValue("agent-id"));
        configure.setStatUrl((String) commandLine.getOptionValue("stat-url"));
        configure.setAppName((String) commandLine.getOptionValue(ArthasConstants.APP_NAME));
        return configure;
    }
```

我们重点关注`configure.setArthasAgent((String) commandLine.getOptionValue("agent"));`这一行代码，从这里可以看出，我们的 agent 就是启动脚本中`agent`这个选项后面的值了。

打开项目工程，找到`agent`这个包，看到里面只有两个类，一个类是一个类加载器，另一个类中我们可以看到熟悉的`premain`和`agentmain`两个函数了，至此我们就找到了整个 arthas 执行的入口。

![arthas_agent入口](https://gitee.com/cincat/picgo/raw/master/img/arthas_agent入口.png)

## arthas 整体架构

我们可以先看下 arthas server的核心类图

![image-20201222232138572](https://gitee.com/cincat/picgo/raw/master/img/image-20201222232138572.png)

如果用领域的思想来看待artahs的设计架构的话，无疑它是一款优秀的软件，从大的方面来说，他分为4个域，分别是：
#### 服务器的抽象
这个领域抽象的主要是arthas命令的执行环境，arthas采用的是传统的client-server的架构，但是server层的环境也是很复杂的，为了兼容不同的网络传输协议，以及统一整个命令的执行流程，作者对这一层进行了抽象，在稍后的文章中，我会带大家看一看这一领域的详细设计，总而言之，这一层回答的问题是，在一个client-server架构下，大家敲击的命令如何到server中。

#### 命令的抽象
arthas所支持的命令有两类：
+ 一类是纯观察JVM信息或者操作arthas的命令如：`VersionCommand`,`JvmCommand`,`QuitCommand等`
+ 一类是要对目标方法进行插装，如`WatchCommand`, `TraceCommand`等，这些命令是arthas的核心领域资产，我们在后续的文章中也会进行着重分析

arthas支持这么多的命令，如果不用一个好的设计组织起来，任由`if-else`横行是万万不行的，为此，arthas在接受到命令后会生成一个`Job`，具体的命令执行交由`Job`来做，也因此形成了命令执行的这个子领域。因为命令执行这个子领域是依赖于命令触发的，所以我会放在命令抽象这个领域中讲。

#### 代码插装领域
这个领域是属于arthas的核心资产，对于一些需要插装才能获取到的信息，我们可以看下arthas是如何操作字节码的，如何在运行时获取方法上下文的信息，并且绑定到插装方法上的，阅读这部分需要对字节码有一定的了解，不过我会尽可能画出每次命令执行的堆栈操作，力图简洁明了。

字节码操作这部分是基于`ASM`框架的，不过也抽象出来了一个独立的框架`bytekit`用以更方便的做字节码操作，这个框架的设计也是可圈可点，完全屏蔽了`ASM`底层，通过精心设计的注解，也是达到了简介易用的目的。

最后，本系列arthas的源码解读基于，arthas 3.4.3