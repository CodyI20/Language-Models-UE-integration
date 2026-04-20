# Conclusion

WIP

---

The following conclusion is drawn directly from the development plan document (page 45).

The integration of AI into Unreal Engine is made accessible by the large number of existing plugins and third-party tools. However, this accessibility does not automatically translate to the processing speed required for real-time VR interactions.

The SLM/LLM used for text-based command interpretation averaged a response time of 1.5 seconds — relatively fast in isolation, but not acceptable for the kind of application RE-liON is building, where real-time interaction is the key differentiator. The Whisper API for speech transcription sits at an even slower average of approximately 4 seconds. Together, this creates a total pipeline delay of roughly 5.5 seconds, which is 10 times slower than the acceptable scenario.

This was measured in ideal conditions: a single user, a quiet environment, button-initiated recording, and short voice commands. In a real VR deployment with an open microphone and multiple simultaneous users, Whisper would take significantly longer to isolate, transcribe, and pass the correct speaker's words through the full pipeline.

Because of this, the integration of language models for the targeted application, in the current technical context and with the current purpose, is not a feasible real-time solution.

The semantic parsing system — using the all-MiniLM-L6-v2 model directly inside UE via NNE — represents the most successful outcome of the project: it removed the need for a large external LLM for the command interpretation path entirely, and it runs in a fraction of the time. The Piper TTS integration via the PiperCLI-Unreal plugin successfully enables on-device speech synthesis without internet dependency. The Speech-to-Text plugin, once CUDA acceleration was confirmed and packaging issues resolved, delivers approximately 0.84-second transcription times on GPU.

The remaining challenges are in open microphone reliability, TTS naturalness, and reducing the combined pipeline latency to below the human perception threshold for real-time interaction.
