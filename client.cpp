
#include <iostream>
#include <boost/array.hpp>
#include <boost/shared_array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include "portaudio.h"
#include "opus.h"

#define SAMPLE_RATE   (48000)
#define FRAMES_PER_BUFFER  (240)



#define BUFFER_SIZE   (16)

struct sAudioBuffer {
    int error;
    OpusDecoder *dec;
    int length[BUFFER_SIZE];
    unsigned char buffer[BUFFER_SIZE][4000];
    int readerIndex;
    int lastSequenceId;
};


static int audioCallback( const void *inputBuffer, void *outputBuffer,
                            unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void *userData ){

    sAudioBuffer *data = (sAudioBuffer*)userData;
    float *out = (float*)outputBuffer;

    (void) timeInfo;
    (void) statusFlags;
    (void) inputBuffer;


    unsigned char* frame = nullptr;
    if(data->length[data->readerIndex] != 0){
        frame = data->buffer[data->readerIndex];
    }


    opus_decode_float(data->dec,
    frame,
    data->length[data->readerIndex],
    out,
    framesPerBuffer,
    0
    );
    data->length[data->readerIndex] = 0;

    if(++data->readerIndex >= BUFFER_SIZE){
        data->readerIndex = 0;
    }

    return paContinue;
}


static void StreamFinished( void* userData ){
    //sAudioBuffer *data = (sAudioBuffer*) userData;
}
boost::array<unsigned char, 1<<15> send_arr;
boost::array<unsigned char, 1<<15> recv_arr;
class sAudioReceiver {
private:
    sAudioBuffer* audiobuf;
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint receiver_endpoint;
    boost::asio::ip::udp::endpoint remote_endpoint;
    boost::posix_time::seconds ping_interval;
    boost::asio::deadline_timer* ping_timer;
public:
    bool synchronized = false;

    sAudioReceiver(boost::asio::io_service& io_service, sAudioBuffer* audiobuf, char* host, int port) : audiobuf(audiobuf), socket(io_service), ping_interval(boost::posix_time::seconds(1)) {
        printf("Creating sAudioReceiver on %s:%d\n", host, port);
        socket.open(boost::asio::ip::udp::v4());

        receiver_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(host), port);


        std::string thx("ihazo");
        std::copy(thx.begin(), thx.end(), send_arr.begin());
        printf("Created sAudioReceiver\n");
        send_request();
        start_receive();
        printf("Sent request\n");

        ping_timer = new boost::asio::deadline_timer(socket.get_io_service(), ping_interval);
        start_timer();
    }
    void start_receive(){
        socket.async_receive_from(
            boost::asio::buffer(recv_arr), receiver_endpoint,
            boost::bind(
                &sAudioReceiver::handle_receive,
                this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred
            )
        );
    }

    void start_timer(){
        ping_timer->expires_at(ping_timer->expires_at() + ping_interval);
        ping_timer->async_wait(boost::bind(
            &sAudioReceiver::handle_timer,
            this,
            boost::asio::placeholders::error
        ));

    }
    void handle_receive(const boost::system::error_code& error, std::size_t recv_len){
        if (!error || error == boost::asio::error::message_size){

            if(recv_len > 5){

                int sequenceId = *((int32_t*) &recv_arr[0]);
                if(sequenceId == 0){
                    std::cout << "Reader placed\n";
                    audiobuf->readerIndex = BUFFER_SIZE-1;
                    //synchronized = true;
                }else{
                    if(sequenceId != audiobuf->lastSequenceId+1){
                        std::cout << "Packet loss/displacement\n";
                    }
                }
                audiobuf->lastSequenceId = sequenceId;
                int audioLen = recv_len-4;
                int writerIndex = sequenceId % BUFFER_SIZE;
                if(writerIndex == audiobuf->readerIndex){
                    audiobuf->readerIndex = (BUFFER_SIZE+writerIndex-1)%BUFFER_SIZE;
                    std::cout << "Synchronizing\n";
                }

                audiobuf->length[writerIndex] = audioLen;
                unsigned char* audio = &recv_arr[4];
                memcpy(audiobuf->buffer[writerIndex], audio, audioLen);

                //std::cout << "recv_len: " << recv_len << "\n";


                //std::cout <<  opus_packet_get_nb_samples(audio, recv_len, SAMPLE_RATE) << "\n";

                /*
                for(int i = 0; i<BUFFER_SIZE; i++){

                    if(sequenceId % BUFFER_SIZE == i){
                        std::cout << "+";
                    }else if(audiobuf->readerIndex == i){
                        std::cout << "-";
                    }else if(audiobuf->length[i] != 0){
                        std::cout << "=";
                    }else{
                        std::cout << " ";
                    }


                }
                std::cout << "\n";*/

                //std::cout << sequenceId << "\n";*/
                std::cout.flush();
            }
            /**/
            start_receive();
        }else{
            std::cout << error.message() << "\n";
        }

    }
    void send_request(){
        socket.async_send_to(boost::asio::buffer(send_arr, 5), receiver_endpoint,
            boost::bind(&sAudioReceiver::handle_send, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred
            )
        );
    }
    void handle_timer(const boost::system::error_code& error){
        if (!error || error == boost::asio::error::message_size){
            send_request();
            start_timer();
        }else{
            std::cout << "timer error: " << error.message() << "\n";
        }

    }


    void handle_send(const boost::system::error_code& error, std::size_t){
        if (!error || error == boost::asio::error::message_size){

        }
    }


};


void serveClient(char* host, sAudioBuffer* audiobuf){
    printf("Starting listener\n");
    boost::asio::io_service io_service;
    sAudioReceiver receiver(io_service, audiobuf, host, 42381);

    io_service.run();
}


int main(int argc, char* argv[]){
    char* host = "127.0.0.1";
    if (argc != 2){
        std::cerr << "Usage: client <host>" << std::endl;
        return 1;
    }else{
        host = argv[1];
    }
    PaStreamParameters outputParameters;
    PaStream *stream;
    PaError err;
    sAudioBuffer* audiobuf;
    audiobuf = new sAudioBuffer();
    audiobuf->dec = opus_decoder_create(SAMPLE_RATE, 2, &audiobuf->error);
    if(audiobuf->error != OPUS_OK){
        std::cerr << "opus: could not create decoder" << std::endl;
        return 2;
    }

    printf("SR = %d, BufSize = %d\n", SAMPLE_RATE, FRAMES_PER_BUFFER);


    audiobuf->readerIndex = 0;

    err = Pa_Initialize();
    if( err != paNoError ) goto error;

    outputParameters.device = Pa_GetDefaultOutputDevice();
    if (outputParameters.device == paNoDevice){
        fprintf(stderr,"Error: No default output device.\n");
        goto error;
    }
    outputParameters.channelCount = 2;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
            &stream,
            NULL,
            &outputParameters,
            SAMPLE_RATE,
            FRAMES_PER_BUFFER,
            paClipOff,
            audioCallback,
            audiobuf);
    if( err != paNoError ) goto error;


    err = Pa_SetStreamFinishedCallback( stream, &StreamFinished );
    if( err != paNoError ) goto error;

    err = Pa_StartStream( stream );
    if( err != paNoError ) goto error;



    try{
        serveClient(host, audiobuf);
    }catch (std::exception& e){
        std::cerr << e.what() << std::endl;
    }


    err = Pa_StopStream( stream );
    if( err != paNoError ) goto error;

    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto error;

    Pa_Terminate();

    delete audiobuf;
    return 0;
error:
    Pa_Terminate();
    fprintf( stderr, "An error occured while using the portaudio stream\n" );
    fprintf( stderr, "Error number: %d\n", err );
    fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
    return err;
}
