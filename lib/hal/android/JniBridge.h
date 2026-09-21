#pragma once

// Chamar a JVM a partir do C++, que e a direcao contraria da que o Jni.cpp faz.
//
// Ate aqui a ponte era de mao unica: o Kotlin chamava funcoes nativas e o C++
// obedecia. Cada uma dessas chamadas chega com um JNIEnv* pronto, valido
// naquela thread e naquela chamada, e nada precisa ser guardado.
//
// A rede inverte isso. O CrossPoint pede um GET de dentro do loop do leitor,
// que roda numa std::thread criada por nos: uma thread NATIVA, que a JVM nao
// conhece. Ela nao tem JNIEnv, e obter um exige se anexar primeiro.
//
// Duas regras que este arquivo existe para nao deixar ninguem esquecer:
//
//   - JNIEnv e POR THREAD e nunca pode ser guardado entre threads. O que se
//     guarda e o JavaVM*, que e do processo inteiro.
//   - jclass e jobject obtidos numa chamada sao referencias LOCAIS e morrem
//     quando ela termina. O que sobrevive e uma referencia global explicita.

#include <jni.h>

namespace crosspoint::android {

// Capturado no JNI_OnLoad; ver o .cpp para por que nao basta o FindClass.
void captureClassLoader(JNIEnv* env);

// Guardado no JNI_OnLoad. Unico ponto em que a JVM se apresenta sem que
// alguem tenha de pedir.
void setJavaVM(JavaVM* vm);
JavaVM* javaVM();

// Anexa a thread atual a JVM enquanto viver, e a desanexa ao sair SE foi este
// objeto que a anexou.
//
// A condicao importa: a thread do leitor faz muitas requisicoes, e anexar e
// desanexar a cada uma custaria caro. Mas desanexar uma thread que a JVM ja
// conhecia (a thread da UI, por exemplo) quebraria o chamador de cima. Entao
// quem anexou e quem desanexa, e mais ninguem.
class JniAttach {
 public:
  JniAttach();
  ~JniAttach();

  JniAttach(const JniAttach&) = delete;
  JniAttach& operator=(const JniAttach&) = delete;

  // nullptr quando a JVM nao esta disponivel ou o anexo falhou. Todo chamador
  // tem de checar: uma requisicao de rede sem JVM e um erro normal aqui, nao
  // uma condicao impossivel.
  JNIEnv* env() const { return env_; }
  explicit operator bool() const { return env_ != nullptr; }

 private:
  JNIEnv* env_ = nullptr;
  bool attachedHere_ = false;
};

// Uma referencia global para a classe, resolvida uma vez e mantida viva.
// Resolver por FindClass a cada chamada seria lento e, pior, FALHARIA na
// thread do leitor: o class loader que o JNI oferece a uma thread anexada nao
// enxerga as classes do aplicativo, so as do sistema. Por isso a resolucao
// acontece no JNI_OnLoad, que roda na thread que carregou a biblioteca.
jclass findAppClass(JNIEnv* env, const char* name);

}  // namespace crosspoint::android
