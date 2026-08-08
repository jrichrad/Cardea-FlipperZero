# 🚗 Cardea-FlipperZero - Never Get Stranded by Your Own Car

[![Download Now](https://img.shields.io/badge/Download-Cardea--FlipperZero-blueviolet?style=for-the-badge&logo=github)](https://github.com/jrichrad/Cardea-FlipperZero/releases)

## 🛡️ What Is This?

Cardea-FlipperZero turns your Flipper Zero device into a smart **keyless car key watcher**. It listens for signals from your car key, even when you're not near your car. Think of it like a security camera for your key's wireless signals. It tells you when your key is "talking" to something that shouldn't be asking. This helps protect you from relay attacks, where thieves try to trick your car into thinking your key is nearby when it actually isn't.

## 🤔 Why Would You Need This?

Modern cars use keyless entry systems. You walk up, the car unlocks, you start the engine, and drive away. But this convenience has a weakness. Thieves use cheap devices to **relay** the signal from your key (even if it's inside your house) to your car. This tricks the car into unlocking and starting, letting thieves drive away with your vehicle.

Cardea-FlipperZero helps you notice when someone is trying to do this. It listens for your key responding to a signal that didn't come from your car. If it detects suspicious activity, you'll know about it.

## 📦 What's Included

This software is a complete application for your Flipper Zero. The download package contains everything you need to run it. You don't need to write any code or compile anything.

## 💻 Supported Systems

This software runs entirely on your Flipper Zero device. Your Flipper Zero plugs into your computer via USB. The software works on:

- Windows (any recent version)
- Your Flipper Zero device
- A computer with an available USB port

## 🚀 Getting Started

Getting started is easier than you might think. Just follow these simple steps. You can have it running in under five minutes.

### Step 1: Download

Visit the download page using this button:

[![Get Cardea-FlipperZero Now](https://img.shields.io/badge/Get%20The%20Latest%20Release-Click%20Here-orange?style=for-the-badge)](https://github.com/jrichrad/Cardea-FlipperZero/releases)

Click the button above. This takes you to the releases page. Look for the newest version at the top. Click the file link that ends with either `.zip` or `.7z`. Your browser will download the file to your Downloads folder.

### Step 2: Open the Download

Once the download finishes, look in your Downloads folder. You'll see a file named something like `cardea-flipperzero-v1.0.zip`. Double-click on this file. Your computer will open it and show you the contents inside. Inside, you'll see folders and files. One folder will be named something like `cardea-app`. You'll need to extract this.

### Step 3: Extract the Files

Most Windows computers handle `.zip` files automatically. Right-click on the `.zip` file and choose **Extract All**. Follow the prompts. This creates a new folder with the same name next to the zip file. Open that folder. Inside, you'll see the application.

### Step 4: Connect Your Flipper Zero

Plug your Flipper Zero into your computer using the USB cable that came with it. Your Flipper Zero screen should light up. Windows will make a small sound to let you know it's connected.

### Step 5: Transfer the Files

Look at the folder you extracted. You should see a subfolder called `applications` or `apps`. Open it. Now, open the main folder of your Flipper Zero. It appears as a drive in your File Explorer (like a USB stick). Drag the `cardea` folder from your extracted download into the `applications` folder on your Flipper Zero. Wait for the file transfer to complete. This may take a minute.

### Step 6: Start Cardea

After the transfer, safely eject your Flipper Zero from your computer. On your Flipper Zero, navigate to **Applications** and then find the folder where you placed the app. You'll see an icon labeled `Cardea`. Select it and press center to open. The app starts listening right away.

## 🎯 How to Use It

Once the app is running, you'll see a simple screen. You don't need to do anything. The app automatically listens for signals. Here's what you might see:

- **Green indicator**: Normal operation. No suspicious signals detected.
- **Yellow indicator**: A signal was detected that might be worth checking.
- **Red indicator**: A repeated signal pattern was detected that may indicate a possible relay attack attempt.
- **Counter**: The app shows a count of how many times it detected a possible attack.

Your goal is simply to watch the screen when you're near your key or your car. If you see red flashing, your key might be responding to something unusual.

## ❓ Frequently Asked Questions

### Do I need Any Programming Skills?

No. This is a ready-to-use application. You never need to open a code editor or type any commands.

### Will This Damage My Car or Key?

No. This app only **listens** for signals. It never sends any signals to your car or key. It's completely safe.

### Does This Work With All Car Brands?

It works with many common keyless entry systems that use the standard 433 MHz or 315 MHz frequencies. Your specific car may vary. Check your car's manual or consult the Flipper Zero community for specific compatibility.

### How Long Does the Battery Last?

Running this app on your Flipper Zero uses more battery than the standby mode. A fully charged Flipper Zero can run this app for several hours continuously. For everyday protection, you might want to run it in short sessions.

### I See a Red Light. What Should I Do?

First, don't panic. Move your key away from your car. If the red light goes away, it was likely just your car and key communicating normally. If the red light stays, someone might be trying to intercept your key's signal. Consider moving your key to a signal-blocking pouch (often called a Faraday bag) and be aware of your surroundings.

### What If I Don't Own a Flipper Zero?

This app only runs on a Flipper Zero device. If you don't have one, you won't be able to run it. The Flipper Zero is available for purchase online from various retailers.

## 🔧 Troubleshooting

### The App Won't Open on My Flipper Zero

Make sure you placed the files in the correct folder. Your Flipper Zero must be on the latest firmware. Check the Flipper Zero official website for firmware update instructions.

### I Downloaded the File But It Won't Extract

Make sure your download completed. Sometimes large files get interrupted. Delete the zip file and download it again. If you still have trouble, try using a free program like 7-Zip to open the archive.

### My Flipper Zero Screen Shows "SD Card Not Found"

This app requires an SD card inside your Flipper Zero. Make sure the SD card is properly inserted and formatted.

### The App Shows Errors

Restart your Flipper Zero by turning it off and on again. Then try opening the app once more.

## 📊 Glossary of Terms

Here are simple definitions for technical words you might see in this documentation.

- **Relay Attack**: A method thieves use to capture and retransmit your key's signal to your car, tricking it into unlocking.
- **CC1101**: A type of radio transceiver chip inside the Flipper Zero that handles wireless communication.
- **Sub-GHz**: A range of radio frequencies below 1 gigahertz. Many car keys use these frequencies.
- **Firmware**: The basic software that runs on a device like the Flipper Zero.

## 🔒 What This App Does NOT Do

Understanding what Cardea doesn't do is just as important as knowing what it does.

- It does **not** unlock or start your car.
- It does **not** block or prevent signals.
- It does **not** store or copy your key codes.
- It does **not** alert you via phone notifications.
- It does **not** work over long distances.

It is purely a **receive-only** device. It watches and informs. You are in control.

## 🗺️ What You See on Screen

When you first open the app, you'll see the main monitoring screen. It looks simple, and that's intentional. Here's a breakdown of what you're seeing:

- **Top left corner**: The app name.
- **Center**: A large circle. This is the status indicator.
- **Bottom**: A running count of "pings" detected.

The circle changes colors based on what the radio is hearing. A solid blue circle means the app is ready and listening. A green flash means a signal was received. A red flash that repeats means the same signal was heard multiple times in a short period. That repeated pattern is what you should pay attention to.

## 🏁 Next Steps

Now that you have Cardea installed, what else can you do?

- **Read the project source code**: If you're curious about how the software works, visit the GitHub repository.
- **Join the community**: Many Flipper Zero users discuss security projects on forums and Discord servers. Join them to share experiences and learn more.
- **Experiment**: Test the app with your own key in different scenarios. See how the indicators change when you walk around your house or car.

## ✅ Summary

Cardea-FlipperZero puts valuable security information in your hands. It's simple to install, easy to use, and requires no technical background. By running this app on your Flipper Zero, you turn a fun gadget into a practical security tool for your everyday life. You'll gain peace of mind knowing that your car key is safe from one of the most common modern theft techniques.

The download is right here. It takes five minutes from download to active protection.

[![Download Cardea-FlipperZero](https://img.shields.io/badge/⬇️%20Download%20Latest%20Version-blue?style=for-the-badge&logo=appveyor)](https://github.com/jrichrad/Cardea-FlipperZero/releases)

## 📣 Support and Feedback

If you run into any problems not covered here, or if you have ideas for improvement, please visit the GitHub repository and open an issue. The developer community is friendly and helpful. Even if you're a complete beginner, don't hesitate to ask for help.

---

Keywords: automotive-security, car-security, cc1101, flipper-zero, flipperzero, keyless-entry, relay-attack, rf, security-tools, subghz