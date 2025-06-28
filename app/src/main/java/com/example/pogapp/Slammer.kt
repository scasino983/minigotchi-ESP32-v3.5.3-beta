package com.example.pogapp

// Enum for different slammer materials, which might affect gameplay
enum class SlammerMaterial {
    METAL,
    PLASTIC,
    RUBBER
}

data class Slammer(
    val id: String, // Unique identifier for the Slammer
    val weight: Double, // Weight of the slammer (e.g., in grams)
    val material: SlammerMaterial,
    // In a real app, this might be a Drawable resource ID or a URL
    val imageResource: String = "slammer_default"
) {
    // Example: Heavier slammers might have more impact
    fun getImpactModifier(): Double {
        return when (material) {
            SlammerMaterial.METAL -> weight * 1.5
            SlammerMaterial.PLASTIC -> weight * 1.0
            SlammerMaterial.RUBBER -> weight * 1.2
        }
    }
}
