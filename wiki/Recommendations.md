# Recommendations

WIP

---

The following recommendations are drawn directly from the development plan document (page 45).

The integration of language models into Unreal Engine is made easier by the wide range of existing plugins and third-party tools. However, the ecosystem does not yet enable the processing speed needed for real-time VR interaction at scale.

The SLM/LLM responsible for interpreting and matching text commands averaged a response time of 1.5 seconds. The Whisper API for transcribing speech averaged approximately 4 seconds. Combined, this results in an average total delay of 5.5 seconds — approximately 10 times slower than the acceptable latency for the target application.

This was measured in an isolated, ideal environment: one user pressing a button to start recording and giving a short command. In a real deployment with the open microphone enabled and multiple users active at the same time, Whisper would require significantly more time to isolate and transcribe the correct voice before passing it to the LLM for final output.

In case Whisper technology improves, or a more advanced speech-to-text model becomes available, the following features should be implemented before revisiting this integration:

1. Automatic voice pickup by the microphone with noise suppression, to prevent the system from misfiring on background sounds.
2. Linking the current logic to a mediator layer that connects the NLP output directly to animations or other required outputs in the Unreal Engine scene.
