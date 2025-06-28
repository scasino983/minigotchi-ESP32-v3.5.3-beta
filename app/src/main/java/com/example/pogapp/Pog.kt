package com.example.pogapp

data class Pog(
    val id: String, // Unique identifier for the Pog
    var isFaceUp: Boolean = false,
    // In a real app, this might be a Drawable resource ID or a URL
    val faceUpImageResource: String = "pog_face_up_default",
    val faceDownImageResource: String = "pog_face_down_default"
)
