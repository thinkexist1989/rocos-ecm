<!--
 Copyright (c) 2021 'Yang Luo, luoyang@sia.cn'

 This software is released under the MIT License.
 https://opensource.org/licenses/MIT
-->

<div align="center">
  <h1>ROCOS-EtherCAT-Master</h1>
  <blockquote>The EtherCAT Master for ROCOS based on Acontis Ec-Master </blockquote>
</div>


# <table><tr><td bgcolor=#5c7ada>简介</td></tr></table>

ROCOS-EtherCAT-Master(ROCOS-Ecm)是基于Acontis Ec-Master 的EtherCAT主站实现。
其设计之初的目的是能够快速实现6自由度或7自由度机械臂关节的运动控制。主要思路是通过EcMaster从PDO中获取周期性数据，并通过共享内存的方式将数据共享来使用。
目前实现的功能主要包括：

# <table><tr><td bgcolor=#5c7ada>依赖</td></tr></table>
## 实时内核

ROCOS-ECM需要实时内核（RT-Kernel）的支持，最简单的方式就是为Linux使用Preempt-RT实时内核补丁，实时内核配置举例参见：UR实时内核配置[https://github.com/UniversalRobots/Universal_Robots_ROS_Driver/blob/395c0541b20d0da2cd480e2ad85b2100410fb043/ur_robot_driver/doc/real_time.md#L4](https://github.com/UniversalRobots/Universal_Robots_ROS_Driver/blob/395c0541b20d0da2cd480e2ad85b2100410fb043/ur_robot_driver/doc/real_time.md#L4)


> 使用范围
ROCOS-ECM目前只有emllI8254x的网卡驱动，支持大部分Intel Pro/1000的网卡，具体支持的网卡型号可以查看Ec-Master用户手册第321页Supported Network Controller一节，本机的网卡型号可以通过lspci -vvv | grep -i ethernet指令查询

```bash
lspci -vvv | grep -i ethernet
```
## 环境配置
ROCOS-ECM主要依赖以下库
- Boost
- Yaml-cpp
- gflags
```bash
sudo apt update
sudo apt install openssh-server net-tools ethtool rt-tests grub-customizer cmake libxcb-xinerama0-dev libgl1-mesa-dev -y
sudo apt-get install libncurses5-dev libssl-dev build-essential openssl zlibc libelf-dev minizip libidn11-dev libidn11 bison flex dwarves libncurses-dev zstd -y
sudo apt install libboost-all-dev libprotobuf-dev -y
sudo apt install liburdfdom-dev libtinyxml-dev libtinyxml2-dev libconsole-bridge-dev libeigen3-dev -y
sudo apt install libyaml-cpp-dev -y
```


# <table><tr><td bgcolor=#5c7ada>下载</td></tr></table>
```bash
git clone -b dev https://github.com/thinkexist1989/rocos-ecm.git
```


# <table><tr><td bgcolor=#5c7ada>编译及安装</td></tr></table>
>rocos-ecm的CMAKE_INSTALL_PREFIX为/opt/rocos/ecm
```bash
cd rocos-ecm/
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j `nproc`
sudo make install
```
编译好的可执行文件放在了build/bin目录下，其主要文件包括：
- rocos_ecm 主程序文件，运行时需要带一些参数，要以sudo运行
- eni.xml从Ec-Engineer中导出的EtherCAT网络信息文件（EtherCAT Network Information），rocos_ecm就是通过-f参数加载这个文件来解析EtherCAT网络拓扑结构
- ecat_config.yaml 配置文件（需要用户针对性修改），主要是配置主站相关信息以及各个PDO数据对应的名称，后续会详细说明如何修改
- atemsys.ko Ec-Master针对Linux的内核模块，主要包括经过优化的网络协议栈（实时性能更好），需要在运行主程序文件前先加载进内核sudo insmod atemsys.ko，但这一步可以通过脚本自动加载，后续会说明
- libemllI8254x.so Ec-Master公司提供的针对Intel网卡的驱动链接库程序，主程序运行时会链接到此库文件
- initECM.sh及runECM.sh 为了方便程序运行编写的脚本文件，可以通过执行两个脚本文件快速运行主站
- 
# <table><tr><td bgcolor=#5c7ada> 配置</td></tr></table>

- 配置并生成<font color=#1E90FF>eni.xml</font>文件
首先需要配置并生成eni.xml文件，这个工作需要使用Ec-Engineer软件来完成，具体过程可以参考 [https://www.acontis.com/en/ethercat-configuration.html](https://www.acontis.com/en/ethercat-configuration.html)
- 配置ecat_config.yaml文件
ecat_config.yaml大致格式如下（以2个从站构成的EtherCAT拓扑结构为例）：
```bash
robot:
  name: 'sia_robot' # [required] less than ROBOT_NAME_MAXLEN(20 bytes)
  license: '1A111B11-2F22C22D-B33DDBCD'
  loop_hz: 1000     # [required] control frequency
  slave_number: 2 # [required]
  slaves:
   - id: 0 # The order of the slave [required]
     name: joint_1
     inputs: # [optional]
       status_word: 'Status word'
       position_actual_value: 'Position actual value'
       velocity_actual_value: 'Velocity actual value'
       torque_actual_value: 'Torque actual value'
       load_torque_value: 'Analog Input 1'
     outputs: # [optional]
       mode_of_operation: 'Mode of operation'
       control_word: 'Control word'
       target_position: 'Target Position'
       target_velocity: 'Target Velocity'
       target_torque: 'Target Torque'
   - id: 1 # The order of the slave [required]
     name: joint_2
     inputs: # [optional]
       status_word: 'Status word'
       position_actual_value: 'Position actual value'
       velocity_actual_value: 'Velocity actual value'
       torque_actual_value: 'Torque actual value'
       load_torque_value: 'Analog Input 1'
     outputs: # [optional]
       mode_of_operation: 'Mode of operation'
       control_word: 'Control word'
       target_position: 'Target Position'
       target_velocity: 'Target Velocity'
       target_torque: 'Target Torque'
```
采用YAML格式，所有的配置都在robot字段下，主要包括以下配置项：
- name：名称，不要超过20 Bytes，用于区分多个EtherCAT拓扑结构（但通常对于机器人应用就一个主站）
- license：Ec-Master的序列号，根据网卡硬件MAC地址生成的密钥，需要向德国Acontis公司索取（前提是已经购买）
- loop_hz：主站循环周期，通常为1000Hz
- slave_number：从站数量，通常等于机器人关节数量
- slaves：对应的每个从站配置，是一个数组结构，数组的大小必须和slave_number字段相同，其中每个字段都有默认值（默认值是Elmo Twitter中的字段名称），可以选择性配置，如果不需要或者所配置的ENI文件中无此字段，可以不配置。
  - id：从站拓扑结构中的顺序，从0开始（这取决于EtherCAT在扫描拓扑结构时候定下来的顺序，每个id对应一个从站，可以在Ec-Engineer中查看）
  - name：从站名称，这个名称要和URDF文件（rocos-app中）中机器人<joint>标签的name属性对应，在rocos-app中的KDL构建运动链时，会根据从站的名称对应相应的关节
  - inputs：RxPDO，CANopen
    - group_name：
    - status_word：状态字，默认值'Status word'
    - position_actual_value：真实位置，默认值'Position actual value'
    - velocity_actual_value：真实速度，默认值'Velocity actual value'
    - torque_actual_value：真实力矩'Torque actual value'
    - load_torque_value：负载端力矩（力矩传感器数据）'Analog Input 1'
  - outputs：TxPDO，CANopen
    - group_name：
    - mode_of_operation：操作模式（csp,csv,cst），默认值'Mode of operation'
    - control_word：控制字，默认值'Control word'
    - target_position：目标位置，默认值'Target Position'
    - target_velocity：目标速度，默认值'Target Velocity'
    - target_torque：目标力矩，默认值'Target Torque'


# <table><tr><td bgcolor=#5c7ada> 使用及运行</td></tr></table>
1. 不要进入/opt/rocos/ecm/bin目录，否则会找不到配置文件
```bash
 cd /opt/rocos/ecm/bin
```
2. 首先需要卸载网卡，这一步可以通过脚本自动完成，也可以手动加载
  ```bash
  sudo sh initECM.sh
  ```

3. 以管理员身份启动主站，其命令行参数有两种方式：
   
  - 通过命令行标志来配置。主要的命令行参数包括如下
  ```bash
  sudo ./rocos_ecm -config=/opt/rocos/ecm/config/ecat_config_2.yaml -eni=/opt/rocos/ecm/config/eni2.xml -cycle=1000 -perf -auxclk=0 -sp=6000  -link=0 -instance=6 -mode=1     
  ```
  <table>
	<tr>
		<td width=20% bgcolor=#4169E1>标志</td>
		<td width=30% bgcolor=#FF7F50>格式</td>
		<td width=20% bgcolor=#008000>参数类型</td>
    <td bgcolor=#FFD700>说明</td>
	</tr>
	<tr>
		<th ><font color=#1E90FF>-config</font></th>
		<th><font color=#1E90FF>-config (cfgFileName)</font></th>
		<th><font color=#1E90FF>string</font> </th>	
    <th><font color=#1E90FF>Path to ecat_config file. "/opt/rocos/ecm/config/ecat_config.yaml", "Defaults path to ecat_config file in YAML format."</font> </th>	
	</tr>
	<tr>
		<th ><font color=#1E90FF>-eni</font></th>
		<th><font color=#1E90FF>-eni (eniFileName)</font></th>
		<th><font color=#1E90FF>int32</font> </th>	
    <th><font color=#1E90FF>Path to ENI file. Note: On Windows CE absolute file paths are needed "/opt/rocos/ecm/config/eni.xml", "Defaults path to ENI file in XML format."</font> </th>	
	</tr>
	<tr>
		<th ><font color=#1E90FF>-duration</font></th>
		<th><font color=#1E90FF>-duration (time)</font></th>
		<th><font color=#1E90FF>int32</font> </th>	
    <th><font color=#1E90FF>(time): Time in msec, 0 = forever (default = 120000).Running duration in msec. When the time expires, the demo application exits completely. Defaults to 0</font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-cycle</font></th>
		<th><font color=#1E90FF>-cycle(cycle time)</font></th>
		<th><font color=#1E90FF>int32</font> </th>	
    <th><font color=#1E90FF>(cycle time): Bus cycle time in μsec.Specifies the bus cycle time. Defaults to 1000μs (1ms). </font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-verbose</font></th>
		<th><font color=#1E90FF>-verbose (level)</font></th>
		<th><font color=#1E90FF>int32</font> </th>	
    <th><font color=#1E90FF>(level): Verbosity level: 0=off (default), 1..n=more messages. The verbosity level specifies how much console output messages will be generated by the demo application. A high verbosity level leads to more messages. Defaults to 3.</font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-cpuidx</font></th>
		<th><font color=#1E90FF>-cpuidx (affinity)</font></th>
		<th><font color=#1E90FF>nt32</font> </th>	
    <th><font color=#1E90FF>(affinity): 0 = first CPU, 1 = second, ...
The CPU affinity specifies which CPU the demo application ought to  . Defaults to 0.</font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-perf</font></th>
		<th><font color=#1E90FF>-perf</font></th>
		<th><font color=#1E90FF>bool（可以无参数）</font> </th>	
    <th><font color=#1E90FF>Enable max. and average time measurement in μs for all EtherCAT jobs (e.g. ProcessAllRxFrames). Defaults to true.</font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-auxclk</font></th>
		<th><font color=#1E90FF>-auxclk (period)</font></th>
		<th><font color=#1E90FF>int32</font> </th>	
    <th><font color=#1E90FF>(period): Clock period in μs (if supported by Operating System).参数必须大于10， 否则不开启auxclk . Defaults to 0.</font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-sp</font></th>
		<th><font color=#1E90FF>-sp[port]</font></th>
		<th><font color=#1E90FF>int32</font> </th>	
    <th><font color=#1E90FF>If platform has support for IP Sockets, this commandline option enables the Remote API Server to be started with the EcMasterDemo. The Remote API Server is going to listen on TCP Port 6000 (or port parameter if given) and is available for connecting Remote API Clients. This option is included for attaching the EC-Lyser Application to the running master. Defaults to 0xFFFF.</font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-log </font></th>
		<th><font color=#1E90FF>-log [prefix]</font></th>
		<th><font color=#1E90FF>string</font> </th>	
    <th><font color=#1E90FF>Use given file name prefix for log files. Defaults to 1.</font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-link</font></th>
		<th><font color=#1E90FF>-link(no)</font></th>
		<th><font color=#1E90FF>int32</font> </th>	
    <th><font color=#1E90FF>0 is i8254x, 1 is i8255x Hardware: Intel Pro/1000 network adapter card. Defaults to 0. </font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-instance</font></th>
		<th><font color=#1E90FF>-instance (no)</font></th>
		<th><font color=#1E90FF>string</font> </th>	
    <th><font color=#1E90FF>(instance): Device instance , 网卡的顺序，例如有两个网卡。地址分别为04：00.0 06：00.0，如想使用06：00.0作为主站，instance=2 . Defaults to "01::00.0".</font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-cpuidx</font></th>
		<th><font color=#1E90FF>-config (cfgFileName)</font></th>
		<th><font color=#1E90FF>string</font> </th>	
    <th><font color=#1E90FF>Path to ecat_config file. </font> </th>	
	</tr>
  <tr>
		<th ><font color=#1E90FF>-mode</font></th>
		<th><font color=#1E90FF>-mode(no)</font></th>
		<th><font color=#1E90FF>int32</font> </th>	
    <th><font color=#1E90FF>(mode): Mode 0 = Interrupt mode, 1= Polling mode. Defaults to 1.</font> </th>	
	</tr>
   <tr>
		<th ><font color=#1E90FF>-dcmmode</font></th>
		<th><font color=#1E90FF>-dcmmode(no)</font></th>
		<th><font color=#1E90FF>int32</font> </th>	
    <th><font color=#1E90FF>0 = off, 1 = busshift, 2 = mastershift. </font> Defaults to 1.</th>	
	</tr>
</table>

  - 将参数放入配置文件，通过--flagfile参数启动。flagfile里面包含了启动主站所需的配置参数，配置好的flagfile默认放在/opt/rocos/ecm/config目录下
  ```bash 
  sudo ./rocos_ecm -flagfile ../config/ecm.flagfile
  ```
  ```bash
  ## ecm.flagfile
  --config=/opt/rocos/ecm/config/ecat_config_2.yaml
  --eni=/opt/rocos/ecm/config/eni2.xml
  --cycle=1000
  --perf
  --auxclk=0
  --sp=6000
  --link=0
  --instance=6
  --mode=1
  ```




---

:bust_in_silhouette: 
- **Yang Luo (yluo@hit.edu.cn)**
- **Jincheng Sun (rocos_sun@foxmail.com)**