http连接处理类
> 通过主从状态机去处理http请求，主状态机调用从状态机，返回处理结果
> 主状态机根据从状态机,更新自身状态，并决定事响应请求还是继续接收数据
> 从状态机读取数据，并且更新自身状态,传给主状态机
