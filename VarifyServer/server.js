
const grpc = require('@grpc/grpc-js')
const message_proto = require('./proto')
const const_module = require('./const')
const { v4: uuidv4 } = require('uuid');
const emailModule = require('./email');
const redis_module = require('./redis')

/**
 * GetVarifyCode grpc响应获取验证码的服务
 * @param {*} call 为grpc请求 
 * @param {*} callback 为grpc回调
 * @returns 
 */
async function GetVarifyCode(call, callback) {
    console.log("邮箱: ", call.request.email)
    try{
        let query_res = await redis_module.GetRedis(const_module.code_prefix+call.request.email);
        console.log("查询结果: ", query_res)
        if(query_res == null){

        }
        let uniqueId = query_res;
        if(query_res ==null){
            uniqueId = uuidv4();
            if (uniqueId.length > 4) {
                uniqueId = uniqueId.substring(0, 4);
            } 
            let bres = await redis_module.SetRedisExpire(const_module.code_prefix+call.request.email, uniqueId,600)
            if(!bres){
                callback(null, { email:  call.request.email,
                    error:const_module.Errors.RedisErr
                });
                return;
            }
        }

        console.log("生成的验证码: ", uniqueId)
        let text_str =  '您的验证码为'+ uniqueId +'请三分钟内完成注册'
        //发送邮件
        let mailOptions = {
            from: 'jiahaotechfu@163.com',
            to: call.request.email,
            subject: '验证码',
            text: text_str,
        };
    
        let send_res = await emailModule.SendMail(mailOptions);
        console.log("发送结果: ", send_res)

        callback(null, { email:  call.request.email,
            error:const_module.Errors.Success
        }); 
        
 
    }catch(error){
        console.log("捕获到错误: ", error)

        callback(null, { email:  call.request.email,
            error:const_module.Errors.Exception
        }); 
    }
     
}

function main() {
    var server = new grpc.Server()
    server.addService(message_proto.VarifyService.service, { GetVarifyCode: GetVarifyCode })
    server.bindAsync('0.0.0.0:50051', grpc.ServerCredentials.createInsecure(), () => {
        console.log('验证服务器已启动')        
    })
}

main()
