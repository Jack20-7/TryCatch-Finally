#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <setjmp.h>


//下面是通过c语言中的setjmp和longjmp来实现 其他语言中的 try-catch

//下面的宏用来实现线程私有成员,用途就是为每一个线程维护一个上下文链表
#define ThreadData                     pthread_key_t
#define ThreadDataSet(key,value)       pthread_setspecific((key),(value))
#define ThreadDataGet(key)             pthread_getspecific((key))
#define ThreadDataCreate(key)          pthread_key_create(&(key),NULL)


#define EXCEPTIN_MESSAGE_LENGTH        512

//记录抛出的异常的名称
typedef struct _Exception{
    const char* name;
}Exception;

Exception SQLException = { "SQLException" };
Exception TimeoutException = { "TimeoutException" };

ThreadData ExceptionStack;      


//存储跳转的上下文
typedef struct ExceptionFrame{
    jmp_buf env;

    int line;
    const char* func;
    const char* file;

    Exception* exception;       //异常名称

    struct _ExceptionFrame* prev;    //上下文通过链表的形式串起来
    char message[EXCEPTIN_MESSAGE_LENGTH + 1];
}ExceptionFrame;

//将链表的第一个节点移出去
#define ExceptionPopStack     \
          ThreadDataSet(ExceptionStack,((ExceptionFrame*)ThreadDataGet(ExceptionStack))->prev)

#define Rethrow                            ExceptionThrow(frame.exception,frame.func,frame.file,frame.line,NULL)
#define Throw(e,cause,...)                 ExceptionThrow(&(e),__func__,__FILE__,__LINE__,cause,##__VA_ARGS__,NULL)

//记录跳转的状态
enum {
    ExceptionEntered = 0,
    ExceptionThrown,
    ExceptionHandled,
    ExceptionFinalized,
};


//Try操作其实就是入栈操作，采用的是尾部插入.线程私有属性中维护的是当前线程上下文链表的尾部节点
#define Try do{                                         \
          volatile int Exception_flag;                  \
          ExceptionFrame frame;                         \
          frame.message[0] = 0;                         \
          frame.prev = (ExceptionFrame*)ThreadDataGet(ExceptionStack);   \
          ThreadDataSet(ExceptionStack,&frame);         \
          Exception_flag = setjmp(frame.env);           \
          if(Exception_flag == ExceptionEntered){       \


#define Catch(e)                                        \
          if(Exception_flag == ExceptionEntered)        ExceptionPopStack; \
          }else if(frame.exception == &(e)){            \
          Exception_flag = ExceptionHandled;

#define Finally                                         \
          if(Exception_flag == ExceptionEntered)    ExceptionPopStack;   \
          }{ \
               if(Exception_flag == ExceptionEntered) \
                  Exception_flag = ExceptionFinalized;

#define EndTry  \
              if(Exception_flag == ExceptionEntered)       ExceptionPopStack ; \
          } if (Exception_flag == ExceptionThrown) Rethrow;                    \ 
          }while(0)

//设置某一个函数只会被调用一次
static pthread_once_t once_control = PTHREAD_ONCE_INIT;

static void init_once(){
    ThreadDataCreate(ExceptionStack);
}


//线程安全的 线程私有属性初始化函数
void ExceptionInit(){
    pthread_once(&once_control,init_once);
}

//抛出异常的函数,其实就是一个出栈函数
void ExceptionThrow(Exception* excep,const char* func,const char* file,int line,const char* cause,...){
    va_list ap;
    ExceptionFrame* frame = (ExceptionFrame*)ThreadDataGet(ExceptionStack);

    if(frame){
        frame->exception = excep;
        frame->func = func;
        frame->line = line;
        frame->file = file;
        if(cause){
            va_start(ap,cause);
            vsnprintf(frame->message,EXCEPTIN_MESSAGE_LENGTH,cause,ap);
            va_end(ap);
        }
        ExceptionPopStack;

        longjmp(frame->env,ExceptionThrown);
    }else if(cause){
        char message[EXCEPTIN_MESSAGE_LENGTH + 1];
        va_start(ap,cause);
        vsnprintf(message,EXCEPTIN_MESSAGE_LENGTH,cause,ap);
        va_end(ap);
        printf("%s: %s\n raised in %s at %s:%d\n",excep->name,message,func ? func : "?",file ? file : "?",line);
    }else{
        printf("%s: %s\n raised in %s at %s:%d\n",excep->name,excep,func ? func : "?",file ? file : "?",line);
    }
}


//提前创建好多个异常
Exception A  = {"AException"};
Exception B =  {"BException"};
Exception C =  {"CException"};
Exception D =  {"DException"};

void* thread(void* args){
    pthread_t selfid = pthread_self();

    Try{
        Throw(A,"A");
    }Catch(A){
        printf("Catch A: %ld\n",selfid);
    }EndTry;

    Try{
        Throw(B,"B");
    }Catch(B){
        printf("Catch B: %ld\n",selfid);
    }EndTry;


    Try{
        Throw(A,"A begin");
        Throw(B,"B begin");
        Throw(C,"C begin");
        Throw(D,"S begin");
    }Catch(A){
        printf("catch A again: %ld\n",selfid);
    }Catch(B){
        printf("catch B again: %ld\n",selfid);
    }Catch(C){
        printf("catch C again: %ld\n",selfid);
    }Catch(D){
        printf("catch D again: %ld\n",selfid);
    }EndTry;
}

#define THREADS   50
int main(){
    ExceptionInit();

    Throw(D,NULL);        //链表中没有上下文可以跳转，所以只是单纯的进行打印
    Throw(C,"null C");    //打印错误信息


    printf("\n\n-> Test1:Try-Catch\n");

    Try{
        Try{
            Throw(B,"recall B");
        }Catch(B){
            printf("recall B\n");
        }EndTry;

        Throw(A,NULL);
    }Catch(A){
        printf("\tResult:ok \n");
    }EndTry;

    printf("->Test1:ok\n");

    printf("Test2:Test Thread-safeness\n");

    int i = 0;
    pthread_t threads[THREADS];
    for(i = 0;i < THREADS;++i){
        pthread_create(&threads[i],NULL,thread,NULL);
    }
    for(i = 0;i < THREADS;++i){
        pthread_join(threads[i],NULL);
    }

    printf("Test2:ok\n");
    return 0;
}